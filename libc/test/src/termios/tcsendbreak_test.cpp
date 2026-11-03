//===-- Tests for tcsendbreak
//----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/limits_macros.h"
#include "src/__support/libc_errno.h"
#include "src/termios/tcsendbreak.h"
#include "test/UnitTest/Test.h"

#include <asm/ioctls.h>
#include <sys/syscall.h>

static unsigned long request, argument;
static int calls, descriptor, error;
static long syscall_number;

namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> termios_test_syscall(long number, int fd, unsigned long cmd,
                                  unsigned long arg) {
  ++calls;
  syscall_number = number;
  descriptor = fd;
  request = cmd;
  argument = arg;
  if (error)
    return Error(error);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

class LlvmLibcTcsendbreakTest : public LIBC_NAMESPACE::testing::Test {
  void SetUp() override {
    calls = error = 0;
    request = argument = 0;
    libc_errno = 0;
  }
};

TEST_F(LlvmLibcTcsendbreakTest, DefaultDuration) {
  const int durations[] = {0, -1, INT_MIN};
  for (int duration : durations) {
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcsendbreak(19, duration), 0);
    EXPECT_EQ(request, static_cast<unsigned long>(TCSBRK));
    EXPECT_EQ(argument, 0UL);
    EXPECT_EQ(descriptor, 19);
    EXPECT_EQ(syscall_number, long(SYS_ioctl));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  EXPECT_EQ(calls, 3);
}

TEST_F(LlvmLibcTcsendbreakTest, PositiveMilliseconds) {
  const int durations[] = {1,
                           99,
                           100,
                           101,
                           199,
                           200,
                           201,
                           (INT_MAX / 100) * 100 - 1,
                           (INT_MAX / 100) * 100};
  for (int duration : durations) {
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcsendbreak(23, duration), 0);
    EXPECT_EQ(request, static_cast<unsigned long>(TCSBRKP));
    EXPECT_EQ(argument, static_cast<unsigned long>(duration / 100 +
                                                   (duration % 100 != 0)));
    EXPECT_EQ(descriptor, 23);
    EXPECT_EQ(syscall_number, long(SYS_ioctl));
    EXPECT_EQ(int(libc_errno), EDOM);
  }
  EXPECT_EQ(calls, 9);
}

TEST_F(LlvmLibcTcsendbreakTest, OverflowBeforeIoctl) {
  for (int duration = (INT_MAX / 100) * 100 + 1;; ++duration) {
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::tcsendbreak(-1, duration), -1);
    EXPECT_EQ(int(libc_errno), EOVERFLOW);
    if (duration == INT_MAX)
      break;
  }
  EXPECT_EQ(calls, 0);
}

TEST_F(LlvmLibcTcsendbreakTest, KernelErrorsAreNotRetried) {
  const int errors[] = {EBADF, ENOTTY, EINTR, EIO};
  const int durations[] = {0, 1};
  for (int e : errors) {
    error = e;
    for (int duration : durations) {
      calls = 0;
      EXPECT_EQ(LIBC_NAMESPACE::tcsendbreak(-1, duration), -1);
      EXPECT_EQ(int(libc_errno), e);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(descriptor, -1);
    }
  }
}
