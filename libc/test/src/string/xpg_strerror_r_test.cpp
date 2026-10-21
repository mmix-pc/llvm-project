//===-- Tests for the POSIX strerror_r provider ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "src/string/__xpg_strerror_r.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

using LlvmLibcXpgStrErrorRTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcXpgStrErrorRTest, CallerStorageAndZero) {
  char buf[128];
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(0, buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "Success");
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(EINVAL, buf, sizeof(buf)), 0);
  EXPECT_STREQ(buf, "Invalid argument");
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = 0;
}

TEST_F(LlvmLibcXpgStrErrorRTest, Bounds) {
  char buf[] = "XXXXXXXXXX";
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(0, nullptr, 0), ERANGE);
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(0, buf + 1, 0), ERANGE);
  EXPECT_STREQ(buf, "XXXXXXXXXX");
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(0, buf + 1, 1), ERANGE);
  EXPECT_EQ(buf[0], 'X');
  EXPECT_EQ(buf[1], '\0');
  EXPECT_EQ(buf[2], 'X');
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(0, buf + 1, 7), ERANGE);
  EXPECT_STREQ(buf + 1, "Succes");
  EXPECT_EQ(buf[8], 'X');
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(0, buf + 1, 8), 0);
  EXPECT_STREQ(buf + 1, "Success");
  EXPECT_EQ(buf[9], 'X');
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = 0;
}

TEST_F(LlvmLibcXpgStrErrorRTest, UnknownErrors) {
  char buf[128];
  char other[128];
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(-1, buf, sizeof(buf)), EINVAL);
  EXPECT_STREQ(buf, "Unknown error -1");
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(INT_MIN, other, sizeof(other)),
            EINVAL);
  EXPECT_STREQ(buf, "Unknown error -1");
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(INT_MAX, other, sizeof(other)),
            EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::__xpg_strerror_r(-1, buf, 2), ERANGE);
  EXPECT_STREQ(buf, "U");
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = 0;
}
