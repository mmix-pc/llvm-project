//===-- Tests for the controlling terminal pathname -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/stdio/ctermid.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

using LlvmLibcCtermidTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcCtermidTest, CallerAndBorrowedStorage) {
  char buffer[L_ctermid + 1];
  for (char &c : buffer)
    c = '!';
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::ctermid(buffer), buffer);
  EXPECT_STREQ(buffer, "/dev/tty");
  EXPECT_EQ(buffer[L_ctermid], '!');
  ASSERT_ERRNO_EQ(EDOM);
  LIBC_NAMESPACE::libc_errno = EDOM;
  char *borrowed = LIBC_NAMESPACE::ctermid(nullptr);
  ASSERT_NE(borrowed, nullptr);
  EXPECT_NE(borrowed, buffer);
  EXPECT_STREQ(borrowed, "/dev/tty");
  buffer[0] = '!';
  EXPECT_EQ(LIBC_NAMESPACE::ctermid(nullptr), borrowed);
  EXPECT_STREQ(borrowed, "/dev/tty");
  ASSERT_ERRNO_EQ(EDOM);
}
