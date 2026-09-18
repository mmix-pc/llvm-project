//===-- Tests for times ---------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/__support/libc_errno.h"
#include "src/sys/times/times.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcTimesTest, AccountingAndNullBuffer) {
  struct tms buffer{};
  libc_errno = 77;
  EXPECT_NE(LIBC_NAMESPACE::times(&buffer), static_cast<clock_t>(-1));
  EXPECT_EQ(int(libc_errno), 77);
  EXPECT_GE(buffer.tms_utime, static_cast<clock_t>(0));
  EXPECT_GE(buffer.tms_stime, static_cast<clock_t>(0));
  EXPECT_NE(LIBC_NAMESPACE::times(nullptr), static_cast<clock_t>(-1));
  EXPECT_EQ(int(libc_errno), 77);
}

TEST(LlvmLibcTimesTest, InvalidBuffer) {
  libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::times(reinterpret_cast<tms *>(-1L)),
            static_cast<clock_t>(-1));
  EXPECT_EQ(int(libc_errno), EFAULT);
}
