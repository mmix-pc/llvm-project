//===-- Word expansion source snapshot tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/freelist_heap.h"
#include "src/wordexp/shell_syntax.h"
#include "src/wordexp/source.h"
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

TEST(LlvmLibcWordSourceTest, OwnedBytesAndScalarSlices) {
  size_t before = live;
  {
    WordSource source;
    char text[] = "'\xc3\xa9'\xe4\xb8\xad";
    ASSERT_EQ(source.assign(text, RestartableConverter(true)), 0);
    text[0] = 'X';
    EXPECT_TRUE(source.bytes() == "'\xc3\xa9'\xe4\xb8\xad");
    ASSERT_EQ(source.scalars().size(), size_t(4));
    EXPECT_EQ(source.scalars()[1], uint32_t(0xe9));
    EXPECT_EQ(source.scalars()[3], uint32_t(0x4e2d));
    EXPECT_EQ(source.units()[1].begin, size_t(1));
    EXPECT_EQ(source.units()[1].end, size_t(3));
    EXPECT_TRUE(*source.slice(1, 2) == "\xc3\xa9");
    EXPECT_TRUE(*source.slice(0, 4) == source.bytes());
    EXPECT_TRUE(source.slice(4, 4)->empty());
    EXPECT_EQ(source.slice(1, 0).error(), EINVAL);
    EXPECT_EQ(source.slice(0, 5).error(), EINVAL);
    EXPECT_TRUE(source.suffix().empty());
    ASSERT_EQ(
        source.assign(source.bytes().substr(1, 2), RestartableConverter(true)),
        0);
    EXPECT_TRUE(source.bytes() == "\xc3\xa9");
    ASSERT_EQ(source.assign("", RestartableConverter(true)), 0);
    EXPECT_TRUE(source.bytes().empty());
    EXPECT_TRUE(source.scalars().empty());
    EXPECT_TRUE(source.suffix().empty());
    EXPECT_TRUE(RestartableConverter(true).initial(source.end_state()));
  }
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcWordSourceTest, InvalidSourcePreservesSnapshot) {
  WordSource source;
  ASSERT_EQ(source.assign("\xff", RestartableConverter(false)), 0);
  const char *old = source.bytes().data();
  string_view invalid[] = {"\xc3", "a\xff", "\xed\xa0\x80", {"a\0b", 3}};
  for (auto bytes : invalid) {
    EXPECT_EQ(source.assign(bytes, RestartableConverter(true)), EILSEQ);
    EXPECT_EQ(source.bytes().data(), old);
    EXPECT_EQ(source.scalars()[0], uint32_t(0xff));
  }
  EXPECT_EQ(source.assign({"", size_t(-1)}, RestartableConverter(true)),
            EOVERFLOW);
  EXPECT_EQ(source.bytes().data(), old);
}

TEST(LlvmLibcWordSourceTest, AllocationFailuresPreserveSource) {
  size_t before = live;
  {
    WordSource source;
    ASSERT_EQ(source.assign("old", RestartableConverter(true)), 0);
    for (int failure = 0; failure != 4; ++failure) {
      const char *old = source.bytes().data();
      size_t owned = live;
      fail_after = failure;
      int error = source.assign("new", RestartableConverter(true));
      fail_after = -1;
      EXPECT_EQ(error, failure < 3 ? ENOMEM : 0);
      EXPECT_EQ(live, owned);
      if (failure < 3) {
        EXPECT_EQ(source.bytes().data(), old);
        EXPECT_TRUE(source.bytes() == "old");
      } else {
        EXPECT_TRUE(source.bytes() == "new");
      }
    }
  }
  EXPECT_EQ(live, before);
}

TEST(LlvmLibcWordSourceTest, ParserOffsetsSelectOriginalCommandBytes) {
  WordSource source;
  ASSERT_EQ(source.assign("$(printf '\xc3\xa9')", RestartableConverter(true)),
            0);
  struct Output {
    WordSource *source;
    bool checked = false;
  } output{&source};
  WordSyntaxSink sink{&output, [](void *p, WordToken token) {
                        auto &out = *static_cast<Output *>(p);
                        if (token.kind == WordToken::Kind::COMMAND) {
                          auto command =
                              out.source->slice(token.begin, token.end);
                          out.checked =
                              command && *command == "printf '\xc3\xa9'";
                        }
                        return 0;
                      }};
  EXPECT_TRUE(bool(
      parse_word_syntax(source.scalars(), false, sink, word_command_syntax())));
  EXPECT_TRUE(output.checked);
}
