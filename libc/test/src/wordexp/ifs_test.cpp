//===-- Word expansion delimiter tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/ifs.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::cpp::string_view;

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0;
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail_after >= 0 && fail_after-- == 0)
    return nullptr;
  void *p = heap.allocate(size);
  live += p != nullptr;
  return p;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail_after >= 0 && fail_after-- == 0)
    return nullptr;
  void *p = heap.realloc(old, size);
  live += p && !old;
  return p;
}

TEST(LlvmLibcWordIFSTest, UnsetEmptyAndOwnedBytes) {
  size_t before = live;
  {
    WordDelimiters ifs;
    ASSERT_EQ(ifs.delimiters().size(), size_t(3));
    EXPECT_TRUE(ifs.delimiters()[0].bytes == " ");
    EXPECT_TRUE(ifs.delimiters()[1].bytes == "\t");
    EXPECT_TRUE(ifs.delimiters()[2].bytes == "\n");
    char value[] = ":\xc3\xa9";
    ASSERT_EQ(ifs.assign(true, value, RestartableConverter(true)), 0);
    value[0] = 'X';
    ASSERT_EQ(ifs.delimiters().size(), size_t(2));
    EXPECT_TRUE(ifs.delimiters()[0].bytes == ":");
    EXPECT_TRUE(ifs.delimiters()[1].bytes == "\xc3\xa9");
    EXPECT_FALSE(ifs.delimiters()[1].whitespace);
    // A previously owned view remains valid until the replacement commits.
    ASSERT_EQ(
        ifs.assign(true, ifs.delimiters()[1].bytes, RestartableConverter(true)),
        0);
    EXPECT_TRUE(ifs.delimiters()[0].bytes == "\xc3\xa9");
    ASSERT_EQ(ifs.assign(true, "", RestartableConverter(true)), 0);
    EXPECT_TRUE(ifs.delimiters().empty());
    ASSERT_EQ(ifs.assign(false, "ignored", RestartableConverter(true)), 0);
    EXPECT_EQ(ifs.delimiters().size(), size_t(3));
  }
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcWordIFSTest, StandardWhitespaceOnly) {
  WordDelimiters ifs;
  ASSERT_EQ(ifs.assign(true, " \t\n\r\v\f\xc2\xa0", RestartableConverter(true)),
            0);
  ASSERT_EQ(ifs.delimiters().size(), size_t(7));
  for (size_t i = 0; i < 7; ++i)
    EXPECT_EQ(ifs.delimiters()[i].whitespace, i < 3);
}

TEST(LlvmLibcWordIFSTest, MalformedInputPreservesPriorSelection) {
  WordDelimiters ifs;
  ASSERT_EQ(ifs.assign(true, ":", RestartableConverter(true)), 0);
  string_view invalid[] = {"\xff", "\xc3", {"a\0b", 3}};
  for (auto text : invalid) {
    EXPECT_EQ(ifs.assign(true, text, RestartableConverter(true)), EILSEQ);
    ASSERT_EQ(ifs.delimiters().size(), size_t(1));
    EXPECT_TRUE(ifs.delimiters()[0].bytes == ":");
  }
  ASSERT_EQ(ifs.assign(true, "\xff", RestartableConverter(false)), 0);
  EXPECT_TRUE(ifs.delimiters()[0].bytes == "\xff");
}

TEST(LlvmLibcWordIFSTest, AllocationFailureIsTransactional) {
  size_t before = live;
  for (int failure = 0; failure != 3; ++failure) {
    {
      WordDelimiters ifs;
      ASSERT_EQ(ifs.assign(true, ":", RestartableConverter(true)), 0);
      size_t owned = live;
      fail_after = failure;
      int error = ifs.assign(true, "abc", RestartableConverter(true));
      fail_after = -1;
      if (failure < 2) {
        EXPECT_EQ(error, ENOMEM);
        EXPECT_EQ(live, owned);
        ASSERT_EQ(ifs.delimiters().size(), size_t(1));
        EXPECT_TRUE(ifs.delimiters()[0].bytes == ":");
      } else {
        EXPECT_EQ(error, 0);
        EXPECT_EQ(ifs.delimiters().size(), size_t(3));
      }
    }
    EXPECT_EQ(live, before);
  }
}

TEST(LlvmLibcWordIFSTest, SplittingDoesNotDecodeExpansionBytes) {
  WordDelimiters ifs;
  ASSERT_EQ(ifs.assign(true, "\xc3\xa9", RestartableConverter(true)), 0);
  struct Output {
    char bytes[8]{};
    size_t used = 0, fields = 0;
  } output;
  FieldSink sink{&output,
                 [](void *p, string_view bytes, bool) -> int {
                   auto &out = *static_cast<Output *>(p);
                   if (bytes.size() > sizeof(out.bytes) - out.used)
                     return EOVERFLOW;
                   for (char c : bytes)
                     out.bytes[out.used++] = c;
                   return 0;
                 },
                 [](void *p) -> int {
                   ++static_cast<Output *>(p)->fields;
                   return 0;
                 }};
  ExpansionPart parts[] = {{"\xff\xc3\xa9\xfe", true, false}};
  auto result = split_word_fields(parts, ifs.delimiters(), sink);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, size_t(2));
  EXPECT_EQ(output.fields, size_t(2));
  EXPECT_EQ(output.used, size_t(2));
  EXPECT_TRUE(string_view(output.bytes, output.used) == "\xff\xfe");
}
