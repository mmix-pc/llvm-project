//===-- Private BSD regex allocation tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/freelist_heap.h"
#include "src/regex/bsd_regex.h"
#include "test/UnitTest/Test.h"

namespace {
LIBC_NAMESPACE::FreeListHeapBuffer<1 << 20> heap;
int fail_after = -1;
size_t live = 0, attempts = 0;
bool fail() {
  ++attempts;
  return fail_after >= 0 && fail_after-- == 0;
}
struct Expression {
  llvm_regex_t value{};
  ~Expression() { __llvm_libc_bsd_regfree(&value); }
};
} // namespace

extern "C" void *__wrap_malloc(size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.allocate(size);
  live += p != nullptr;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail())
    return nullptr;
  void *p = heap.realloc(old, size);
  live += p && !old;
  return p;
}
extern "C" void __wrap_free(void *p) {
  live -= p != nullptr;
  heap.free(p);
}

TEST(LlvmLibcRegexAllocationTest, RepetitionStripGrowthFailure) {
  const char *patterns[] = {"(abcdef){8,12}", "(a|b|c|d)+", "a{0,12}",
                            "(a(bc)?){4,8}", "[[.a.]]{32}"};
  for (const char *pattern : patterns) {
    size_t count, before = live;
    {
      Expression re;
      attempts = 0;
      ASSERT_EQ(__llvm_libc_bsd_regcomp(&re.value, pattern, REG_EXTENDED), 0);
      count = attempts;
    }
    EXPECT_EQ(live, before);
    for (size_t at = 0; at < count; ++at) {
      {
        Expression re;
        fail_after = static_cast<int>(at);
        int error = __llvm_libc_bsd_regcomp(&re.value, pattern, REG_EXTENDED);
        fail_after = -1;
        EXPECT_TRUE(error == 0 || error == REG_ESPACE);
      }
      EXPECT_EQ(live, before);
    }
  }
}
