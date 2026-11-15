//===-- Unittests for sendmsg ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "hdr/types/struct_msghdr.h"
#include "src/sys/socket/sendmsg.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"

using LlvmLibcSendmsgTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcSendmsgTest, RejectsZeroDimensionalVector) {
  struct msghdr message = {};
  EXPECT_EQ(LIBC_NAMESPACE::sendmsg(-1, &message, 0), ssize_t(-1));
  ASSERT_ERRNO_EQ(EMSGSIZE);
}

TEST_F(LlvmLibcSendmsgTest, PositiveDimensionReachesDescriptorValidation) {
  struct iovec vector = {};
  struct msghdr message = {};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  EXPECT_EQ(LIBC_NAMESPACE::sendmsg(-1, &message, 0), ssize_t(-1));
  ASSERT_ERRNO_EQ(EBADF);
}
