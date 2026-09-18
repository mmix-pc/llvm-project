//===-- Tests for ppoll --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/poll/ppoll.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcPpollTest, TimeoutCopy) {
  const timespec timeout{0, 1};
  libc_errno = 77;
  EXPECT_EQ(LIBC_NAMESPACE::ppoll(nullptr, 0, &timeout, nullptr), 0);
  EXPECT_EQ(int(libc_errno), 77);
  EXPECT_EQ(timeout.tv_sec, decltype(timeout.tv_sec)(0));
  EXPECT_EQ(timeout.tv_nsec, decltype(timeout.tv_nsec)(1));
}

TEST(LlvmLibcPpollTest, InvalidTimeout) {
  const timespec invalid[] = {{-1, 0}, {0, -1}, {0, 1000000000}};
  for (auto timeout : invalid) {
    EXPECT_EQ(LIBC_NAMESPACE::ppoll(nullptr, 0, &timeout, nullptr), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
  }
}

TEST(LlvmLibcPpollTest, InvalidDescriptors) {
  const timespec timeout{};
  EXPECT_EQ(LIBC_NAMESPACE::ppoll(nullptr, 1, &timeout, nullptr), -1);
  EXPECT_EQ(int(libc_errno), EFAULT);
}
