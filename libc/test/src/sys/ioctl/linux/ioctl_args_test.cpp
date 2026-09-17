//===-- Tests for Linux ioctl argument forwarding
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/sys_ioctl_macros.h"
#include "ioctl_test_syscall.h"
#include "src/sys/ioctl/ioctl.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
long last_number;
int last_fd, reply;
unsigned long last_request, last_arg;

class LlvmLibcIoctlArgsTest
    : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
public:
  void SetUp() override {
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::SetUp();
    last_number = last_fd = reply = 0;
    last_request = last_arg = 0;
  }
};
} // namespace

namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> ioctl_test_syscall(long number, int fd, unsigned long request,
                                unsigned long arg) {
  last_number = number;
  last_fd = fd;
  last_request = request;
  last_arg = arg;
  if (reply < 0)
    return Error(-reply);
  return reply;
}
} // namespace LIBC_NAMESPACE_DECL

TEST_F(LlvmLibcIoctlArgsTest, NoArgumentRequests) {
  const unsigned long requests[] = {FIOCLEX,   FIONCLEX, TIOCEXCL, TIOCNXCL,
                                    TIOCNOTTY, TIOCCONS, TIOCSBRK, TIOCCBRK};
  int sentinel;
  for (auto request : requests) {
    EXPECT_EQ(LIBC_NAMESPACE::ioctl(7, request), 0);
    EXPECT_EQ(last_arg, 0UL);
    // An extra ignored argument makes unconditional va_arg consumption
    // observable.
    EXPECT_EQ(LIBC_NAMESPACE::ioctl(7, request, static_cast<void *>(&sentinel)),
              0);
    EXPECT_EQ(last_arg, 0UL);
    EXPECT_EQ(last_number, long(SYS_ioctl));
    EXPECT_EQ(last_fd, 7);
    EXPECT_EQ(last_request, request);
  }
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcIoctlArgsTest, PromotedIntegerRequests) {
  const unsigned long requests[] = {TIOCSCTTY, TCSBRK, TCSBRKP, TCXONC, TCFLSH};
  const int values[] = {0, 1, -42, 0x12345678};
  for (auto request : requests) {
    for (int value : values) {
      EXPECT_EQ(LIBC_NAMESPACE::ioctl(7, request, value), 0);
      EXPECT_EQ(last_arg, static_cast<unsigned long>(value));
      EXPECT_EQ(last_request, request);
    }
  }
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcIoctlArgsTest, PointerRequestsAndUnknownExtension) {
  const unsigned long requests[] = {FIONREAD,   FIONBIO,     TIOCGWINSZ,
                                    TIOCSWINSZ, TIOCGPGRP,   TIOCSPGRP,
                                    TCGETS,     0xdeadbeefUL};
  int data;
  reply = 13;
  for (auto request : requests) {
    EXPECT_EQ(LIBC_NAMESPACE::ioctl(7, request, static_cast<void *>(&data)),
              13);
    EXPECT_EQ(last_arg, reinterpret_cast<unsigned long>(&data));
    EXPECT_EQ(last_request, request);
    EXPECT_EQ(LIBC_NAMESPACE::ioctl(7, request, static_cast<void *>(nullptr)),
              13);
    EXPECT_EQ(last_arg, 0UL);
  }
  ASSERT_ERRNO_SUCCESS();
}

TEST_F(LlvmLibcIoctlArgsTest, Errors) {
  const int errors[] = {EBADF, EINVAL, ENOTTY, EFAULT, EPERM, ENOSYS};
  for (int error : errors) {
    reply = -error;
    EXPECT_EQ(LIBC_NAMESPACE::ioctl(-1, TIOCNOTTY), -1);
    EXPECT_EQ(last_fd, -1);
    ASSERT_ERRNO_EQ(error);
    EXPECT_EQ(LIBC_NAMESPACE::ioctl(-1, TIOCSCTTY, 0), -1);
    ASSERT_ERRNO_EQ(error);
    EXPECT_EQ(
        LIBC_NAMESPACE::ioctl(-1, TIOCGWINSZ, static_cast<void *>(nullptr)),
        -1);
    ASSERT_ERRNO_EQ(error);
  }
}
