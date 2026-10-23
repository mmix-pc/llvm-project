//===-- Allocation failure tests for realpath ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/malloc.h"
#include "hdr/limits_macros.h"
#include "src/__support/libc_errno.h"
#include "src/stdlib/realpath.h"
#include "test/UnitTest/Test.h"

namespace {
int allocation_calls;
size_t allocation_size;
bool fail_allocation;
char allocation[PATH_MAX];
} // namespace

static void *realpath_test_malloc(size_t size) {
  ++allocation_calls;
  allocation_size = size;
  return fail_allocation ? nullptr : allocation;
}

// Interpose only this provider's allocation, not the test runner's allocator.
#define malloc realpath_test_malloc
#define realpath allocation_test_realpath
#include "src/stdlib/linux/realpath.cpp"
#undef realpath
#undef malloc

TEST(LlvmLibcRealpathAllocationTest, FailureReturnsENOMEM) {
  fail_allocation = true;
  allocation_calls = 0;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::allocation_test_realpath("/", nullptr), nullptr);
  EXPECT_EQ(allocation_calls, 1);
  EXPECT_EQ(allocation_size, size_t(2));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
}

TEST(LlvmLibcRealpathAllocationTest, CallerBufferNeedsNoAllocation) {
  fail_allocation = true;
  allocation_calls = 0;
  char buffer[PATH_MAX];
  EXPECT_EQ(LIBC_NAMESPACE::allocation_test_realpath("/./../", buffer), buffer);
  EXPECT_STREQ(buffer, "/");
  EXPECT_EQ(allocation_calls, 0);
}

TEST(LlvmLibcRealpathAllocationTest, ResultIsCopiedOnlyOnSuccess) {
  fail_allocation = false;
  allocation_calls = 0;
  allocation[2] = '#';
  EXPECT_EQ(LIBC_NAMESPACE::allocation_test_realpath("/", nullptr), allocation);
  EXPECT_STREQ(allocation, "/");
  EXPECT_EQ(allocation[2], '#');
  EXPECT_EQ(allocation_calls, 1);
  EXPECT_EQ(allocation_size, size_t(2));
}

TEST(LlvmLibcRealpathAllocationTest, InvalidPathDoesNotAllocateOrChangeOutput) {
  fail_allocation = true;
  allocation_calls = 0;
  char buffer[PATH_MAX] = "unchanged";
  EXPECT_EQ(LIBC_NAMESPACE::allocation_test_realpath("", buffer), nullptr);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  EXPECT_STREQ(buffer, "unchanged");
  EXPECT_EQ(allocation_calls, 0);
}
