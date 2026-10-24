//===-- Allocation failure and recovery tests for strdup ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/libc_errno.h"
#include "src/string/strdup.h"
#include "test/UnitTest/Test.h"

namespace {
bool fail_allocation = false;
unsigned hits = 0;
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation) {
    ++hits;
    return nullptr;
  }
  return __real_malloc(size);
}

TEST(LlvmLibcStrdupAllocationTest, FailureAndRecovery) {
  char source[] = "abc";
  for (size_t offset = 0; offset != sizeof(source); ++offset) {
    hits = 0;
    libc_errno = EBUSY;
    fail_allocation = true;
    char *copy = LIBC_NAMESPACE::strdup(source + offset);
    fail_allocation = false;
    EXPECT_EQ(copy, nullptr);
    EXPECT_EQ(hits, 1U);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_STREQ(source, "abc");
    copy = LIBC_NAMESPACE::strdup(source + offset);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy, source + offset);
    EXPECT_STREQ(copy, source + offset);
    ::free(copy);
  }
}
