//===-- Decoded regular expression text tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/regex/decoded_text.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::DecodedRegexText;
using LIBC_NAMESPACE::internal::RestartableConverter;

namespace {
bool fail_allocation = false;
size_t live = 0;
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation)
    return nullptr;
  void *result = heap.allocate(size);
  live += result != nullptr;
  return result;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail_allocation)
    return nullptr;
  void *result = heap.realloc(old, size);
  live += result && !old;
  return result;
}

TEST(LlvmLibcDecodedRegexTextTest, UTF8BytePositionsAndEmptyInput) {
  DecodedRegexText text;
  ASSERT_EQ(text.assign("a\xc3\xa9\xe4\xb8\xad", RestartableConverter(true)),
            0);
  ASSERT_EQ(text.units().size(), size_t(3));
  EXPECT_EQ(text.units()[0].scalar, uint32_t('a'));
  EXPECT_EQ(text.units()[0].begin, regoff_t(0));
  EXPECT_EQ(text.units()[0].end, regoff_t(1));
  EXPECT_EQ(text.units()[1].scalar, uint32_t(0xe9));
  EXPECT_EQ(text.units()[1].begin, regoff_t(1));
  EXPECT_EQ(text.units()[1].end, regoff_t(3));
  EXPECT_EQ(text.units()[2].scalar, uint32_t(0x4e2d));
  EXPECT_EQ(text.units()[2].begin, regoff_t(3));
  EXPECT_EQ(text.units()[2].end, regoff_t(6));
  EXPECT_EQ(text.bytes(), regoff_t(6));
  EXPECT_EQ(text.characters_end(), regoff_t(6));
  ASSERT_EQ(text.assign("", RestartableConverter(true)), 0);
  EXPECT_EQ(text.units().size(), size_t(0));
  EXPECT_EQ(text.bytes(), regoff_t(0));
}

TEST(LlvmLibcDecodedRegexTextTest, CLocaleHighBytesAndInvalidInput) {
  DecodedRegexText text;
  ASSERT_EQ(text.assign("\x80\xff", RestartableConverter(false)), 0);
  ASSERT_EQ(text.units().size(), size_t(2));
  EXPECT_EQ(text.units()[0].scalar, uint32_t(0x80));
  EXPECT_EQ(text.units()[1].scalar, uint32_t(0xff));
  auto *previous = text.units().data();
  const char *invalid[] = {"\xc3",         "\xe4\xb8",         "\xc0\x80",
                           "\xed\xa0\x80", "\xf4\x90\x80\x80", "a\xff"};
  for (const char *input : invalid) {
    EXPECT_EQ(text.assign(input, RestartableConverter(true)), EILSEQ);
    EXPECT_EQ(text.units().data(), previous);
    EXPECT_EQ(text.bytes(), regoff_t(2));
  }
  const char nul[] = {'a', 0, 'b'};
  EXPECT_EQ(text.assign({nul, sizeof(nul)}, RestartableConverter(false)),
            EILSEQ);
  EXPECT_EQ(text.units().data(), previous);
}

TEST(LlvmLibcDecodedRegexTextTest, AllocationRollbackAndSizeOverflow) {
  size_t before = live;
  {
    DecodedRegexText text;
    ASSERT_EQ(text.assign("old", RestartableConverter(true)), 0);
    auto *old = text.units().data();
    size_t allocated = live;
    fail_allocation = true;
    int error = text.assign("new\xc3\xa9", RestartableConverter(true));
    fail_allocation = false;
    EXPECT_EQ(error, ENOMEM);
    EXPECT_EQ(live, allocated);
    EXPECT_EQ(text.units().data(), old);
    EXPECT_EQ(text.units().size(), size_t(3));
    EXPECT_EQ(text.assign({"", size_t(-1)}, RestartableConverter(true)),
              EOVERFLOW);
    EXPECT_EQ(text.units().data(), old);
    ASSERT_EQ(text.assign("new", RestartableConverter(true)), 0);
  }
  EXPECT_EQ(live, before);
}
