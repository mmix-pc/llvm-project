//===-- Linux tcflow and tcflush syscall-boundary tests --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/limits_macros.h"
#include "hdr/termios_macros.h"
#include "src/__support/libc_errno.h"
#include "src/termios/tcflow.h"
#include "src/termios/tcflush.h"
#include "test/UnitTest/Test.h"
#include "test/src/termios/termios_test_syscall.h"

#include <asm/ioctls.h>
#include <sys/syscall.h>

// Compile both provider sources with termios_test_syscall.h force-included,
// as for termios_conversion_test. No host terminal is accessed.
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

class LlvmLibcFlowFlushTest : public LIBC_NAMESPACE::testing::Test {
  void SetUp() override {
    calls = descriptor = error = 0;
    request = argument = 0;
    syscall_number = 0;
    libc_errno = 0;
  }

protected:
  void CheckIoctl(int fd, unsigned long cmd, int selector) {
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(syscall_number, long(SYS_ioctl));
    EXPECT_EQ(descriptor, fd);
    EXPECT_EQ(request, cmd);
    EXPECT_EQ(argument, static_cast<unsigned long>(selector));
  }
};

TEST_F(LlvmLibcFlowFlushTest, InvalidFlowActionsBeforeIoctl) {
  const int actions[] = {-1, TCION + 1, INT_MIN, INT_MAX};
  const int descriptors[] = {-1, 19};
  for (int fd : descriptors) {
    error = fd == -1 ? EBADF : ENOTTY;
    for (int action : actions) {
      calls = 0;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::tcflow(fd, action), -1);
      EXPECT_EQ(int(libc_errno), EINVAL);
      EXPECT_EQ(calls, 0);
    }
  }
}

TEST_F(LlvmLibcFlowFlushTest, InvalidFlushSelectorsBeforeIoctl) {
  const int selectors[] = {-1, TCIOFLUSH + 1, INT_MIN, INT_MAX};
  const int descriptors[] = {-1, 19};
  for (int fd : descriptors) {
    error = fd == -1 ? EBADF : ENOTTY;
    for (int selector : selectors) {
      calls = 0;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::tcflush(fd, selector), -1);
      EXPECT_EQ(int(libc_errno), EINVAL);
      EXPECT_EQ(calls, 0);
    }
  }
}

TEST_F(LlvmLibcFlowFlushTest, LegalFlowActions) {
  const int actions[] = {TCOOFF, TCOON, TCIOFF, TCION};
  for (int action : actions) {
    calls = 0;
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcflow(19, action), 0);
    CheckIoctl(19, TCXONC, action);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST_F(LlvmLibcFlowFlushTest, LegalFlushSelectors) {
  const int selectors[] = {TCIFLUSH, TCOFLUSH, TCIOFLUSH};
  for (int selector : selectors) {
    calls = 0;
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::tcflush(23, selector), 0);
    CheckIoctl(23, TCFLSH, selector);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST_F(LlvmLibcFlowFlushTest, FlowKernelErrorsAreNotRetried) {
  const int actions[] = {TCOOFF, TCOON, TCIOFF, TCION};
  const int errors[] = {EBADF, ENOTTY, EINTR, EIO, EINVAL};
  for (int e : errors) {
    error = e;
    const int fd = e == EBADF ? -1 : 19;
    for (int action : actions) {
      calls = 0;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::tcflow(fd, action), -1);
      CheckIoctl(fd, TCXONC, action);
      EXPECT_EQ(int(libc_errno), e);
    }
  }
}

TEST_F(LlvmLibcFlowFlushTest, FlushKernelErrorsAreNotRetried) {
  const int selectors[] = {TCIFLUSH, TCOFLUSH, TCIOFLUSH};
  const int errors[] = {EBADF, ENOTTY, EINTR, EIO, EINVAL};
  for (int e : errors) {
    error = e;
    const int fd = e == EBADF ? -1 : 23;
    for (int selector : selectors) {
      calls = 0;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::tcflush(fd, selector), -1);
      CheckIoctl(fd, TCFLSH, selector);
      EXPECT_EQ(int(libc_errno), e);
    }
  }
}
