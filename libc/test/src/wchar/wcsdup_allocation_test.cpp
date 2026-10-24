//===-- Allocation and ownership tests for wcsdup -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/libc_errno.h"
#include "src/wchar/wcsdup.h"
#include "test/UnitTest/Test.h"

namespace {
bool fail_allocation = false;
size_t requested_size = 0;
unsigned failures = 0;
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation) {
    requested_size = size;
    ++failures;
    return nullptr;
  }
  return __real_malloc(size);
}

TEST(LlvmLibcWcsdupAllocationTest, FailureAndRecovery) {
  wchar_t source[] = {L'a', L'b', L'c', 0};
  for (size_t offset = 0; offset != 4; ++offset) {
    libc_errno = EBUSY;
    requested_size = failures = 0;
    fail_allocation = true;
    wchar_t *copy = LIBC_NAMESPACE::wcsdup(source + offset);
    fail_allocation = false;
    EXPECT_EQ(copy, nullptr);
    EXPECT_EQ(failures, 1U);
    EXPECT_EQ(requested_size, (4 - offset) * sizeof(wchar_t));
    EXPECT_EQ(int(libc_errno), ENOMEM);
    for (size_t i = 0; i != 3; ++i)
      EXPECT_EQ(source[i], wchar_t(L'a' + i));
    EXPECT_EQ(source[3], wchar_t(0));
    copy = LIBC_NAMESPACE::wcsdup(source + offset);
    ASSERT_NE(copy, nullptr);
    for (size_t i = offset; i != 4; ++i)
      EXPECT_EQ(copy[i - offset], source[i]);
    ::free(copy);
  }
}

TEST(LlvmLibcWcsdupAllocationTest, IndependentOwners) {
  wchar_t source[] = L"abc";
  wchar_t *first = LIBC_NAMESPACE::wcsdup(source);
  ASSERT_NE(first, nullptr);
  wchar_t *second = LIBC_NAMESPACE::wcsdup(source);
  if (!second) {
    ::free(first);
    ASSERT_NE(second, nullptr);
    return;
  }
  EXPECT_NE(first, second);
  source[0] = L'x';
  first[1] = L'y';
  EXPECT_EQ(first[0], L'a');
  EXPECT_EQ(source[1], L'b');
  EXPECT_EQ(second[0], L'a');
  EXPECT_EQ(second[1], L'b');
  ::free(first);
  EXPECT_EQ(second[2], L'c');
  EXPECT_EQ(second[3], wchar_t(0));
  ::free(second);
}
