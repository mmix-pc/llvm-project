//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/__support/common.h"
#include "test/UnitTest/Test.h"

namespace {
char storage[1024];
bool fail_allocate;
bool fail_resize;
int frees;
void *test_malloc(size_t size) {
  return fail_allocate || size > sizeof(storage) ? nullptr : storage;
}
void *test_realloc(void *, size_t size) {
  return fail_resize || size > sizeof(storage) ? nullptr : storage;
}
void test_free(void *) { ++frees; }
} // namespace

// Inject allocation failures into the production implementation without
// overriding the allocator used by the test runner.
#define malloc test_malloc
#define realloc test_realloc
#define free test_free
#define CharVector FaultInjectedCharVector
#include "src/__support/char_vector.h"
#undef CharVector
#undef free
#undef realloc
#undef malloc

TEST(LlvmLibcCharVectorFailureTest, InitialGrowthFailureCanBeRetried) {
  fail_allocate = true;
  fail_resize = false;
  frees = 0;
  {
    LIBC_NAMESPACE::FaultInjectedCharVector v;
    for (int i = 0; i < 62; ++i)
      ASSERT_TRUE(v.append('a'));
    EXPECT_FALSE(v.append('b'));
    EXPECT_FALSE(v.append('c'));
    EXPECT_EQ(v.length(), size_t(62));
    EXPECT_EQ(v.c_str()[61], 'a');
    EXPECT_EQ(v.c_str()[62], '\0');
    fail_allocate = false;
    EXPECT_TRUE(v.append('d'));
    EXPECT_EQ(v.c_str()[62], 'd');
  }
  EXPECT_EQ(frees, 1);
}

TEST(LlvmLibcCharVectorFailureTest, ReallocationPreservesOwnershipAndCapacity) {
  fail_allocate = false;
  fail_resize = true;
  frees = 0;
  {
    LIBC_NAMESPACE::FaultInjectedCharVector v;
    for (int i = 0; i < 126; ++i)
      ASSERT_TRUE(v.append('a'));
    char *before = v.c_str();
    EXPECT_FALSE(v.append('b'));
    EXPECT_FALSE(v.append('c'));
    EXPECT_EQ(v.c_str(), before);
    EXPECT_EQ(v.length(), size_t(126));
    EXPECT_EQ(v.c_str()[125], 'a');
    EXPECT_EQ(v.c_str()[126], '\0');
    fail_resize = false;
    EXPECT_TRUE(v.append('d'));
    EXPECT_EQ(v.c_str()[126], 'd');
  }
  EXPECT_EQ(frees, 1);
}
