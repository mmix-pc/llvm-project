//===-- Tests for tree search allocation failures -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/search/tdelete.h"
#include "src/search/tfind.h"
#include "src/search/tsearch.h"
#include "test/UnitTest/Test.h"

namespace {
bool track = false, fail = false;
unsigned allocations, releases;
int compare(const void *left, const void *right) {
  int a = *static_cast<const int *>(left);
  int b = *static_cast<const int *>(right);
  return (a > b) - (a < b);
}
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (track) {
    ++allocations;
    if (fail)
      return nullptr;
  }
  return __real_malloc(size);
}
extern "C" void __wrap_free(void *ptr) {
  if (track && ptr)
    ++releases;
  __real_free(ptr);
}

TEST(LlvmLibcTSearchAllocationTest, FailurePreservesTreeAndKeys) {
  int keys[] = {3, 1, 5};
  __llvm_libc_tnode *root = nullptr;
  allocations = releases = 0;
  track = fail = true;
  auto *rejected = LIBC_NAMESPACE::tsearch(&keys[0], &root, compare);
  track = false;
  ASSERT_EQ(rejected, nullptr);
  ASSERT_EQ(root, nullptr);
  ASSERT_EQ(allocations, 1U);
  ASSERT_EQ(releases, 0U);

  allocations = releases = 0;
  fail = false;
  track = true;
  auto *first = LIBC_NAMESPACE::tsearch(&keys[0], &root, compare);
  auto *second = LIBC_NAMESPACE::tsearch(&keys[1], &root, compare);
  track = false;
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(allocations, 2U);
  auto *old_root = root;

  fail = track = true;
  rejected = LIBC_NAMESPACE::tsearch(&keys[2], &root, compare);
  auto *duplicate = LIBC_NAMESPACE::tsearch(&keys[0], &root, compare);
  track = false;
  EXPECT_EQ(rejected, nullptr);
  EXPECT_EQ(root, old_root);
  EXPECT_EQ(duplicate, first);
  EXPECT_EQ(allocations, 3U);
  EXPECT_EQ(releases, 0U);
  EXPECT_EQ(LIBC_NAMESPACE::tfind(&keys[0], &root, compare), first);
  EXPECT_EQ(LIBC_NAMESPACE::tfind(&keys[1], &root, compare), second);
  EXPECT_EQ(LIBC_NAMESPACE::tfind(&keys[2], &root, compare), nullptr);

  track = true;
  void *deleted_first = LIBC_NAMESPACE::tdelete(&keys[0], &root, compare);
  void *deleted_second = LIBC_NAMESPACE::tdelete(&keys[1], &root, compare);
  track = false;
  EXPECT_NE(deleted_first, nullptr);
  EXPECT_NE(deleted_second, nullptr);
  EXPECT_EQ(root, nullptr);
  EXPECT_EQ(releases, 2U);
  EXPECT_EQ(keys[0], 3);
  EXPECT_EQ(keys[1], 1);
  EXPECT_EQ(keys[2], 5);
}
