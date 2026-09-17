//===-- Linux termios conversion tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/termios_macros.h"
#include "src/__support/libc_errno.h"
#include "src/termios/cfgetispeed.h"
#include "src/termios/cfgetospeed.h"
#include "src/termios/cfsetispeed.h"
#include "src/termios/cfsetospeed.h"
#include "src/termios/linux/kernel_termios.h"
#include "src/termios/tcgetattr.h"
#include "src/termios/tcsetattr.h"
#include "test/UnitTest/Test.h"

#include <asm/ioctls.h>
#include <sys/syscall.h>

static LIBC_NAMESPACE::kernel_termios kernel;
static unsigned long request;
static int calls, descriptor, error;
static long syscall_number;

namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> termios_test_syscall(long number, int fd, unsigned long cmd,
                                  unsigned long arg) {
  ++calls;
  syscall_number = number;
  descriptor = fd;
  request = cmd;
  if (error)
    return Error(error);
  auto *t = reinterpret_cast<kernel_termios *>(arg);
  if (cmd == TCGETS)
    *t = kernel;
  else
    kernel = *t;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

class LlvmLibcTermiosConversionTest : public LIBC_NAMESPACE::testing::Test {
  void SetUp() override {
    kernel = {};
    calls = error = 0;
    libc_errno = 0;
  }
};

TEST_F(LlvmLibcTermiosConversionTest, GetAttributes) {
  kernel.c_iflag = ICRNL;
  kernel.c_oflag = OPOST;
  kernel.c_lflag = ISIG | ECHO;
  kernel.c_cflag = CS8 | B115200 | (B50 << 16);
  kernel.c_line = 3;
  for (size_t i = 0; i < LIBC_NAMESPACE::KERNEL_NCCS; ++i)
    kernel.c_cc[i] = static_cast<cc_t>(i + 1);
  termios t = {};
  for (auto &c : t.c_cc)
    c = 255;
  ASSERT_EQ(LIBC_NAMESPACE::tcgetattr(19, &t), 0);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(syscall_number, long(SYS_ioctl));
  EXPECT_EQ(descriptor, 19);
  EXPECT_EQ(request, static_cast<unsigned long>(TCGETS));
  EXPECT_EQ(t.c_iflag, kernel.c_iflag);
  EXPECT_EQ(t.c_oflag, kernel.c_oflag);
  EXPECT_EQ(t.c_cflag, kernel.c_cflag);
  EXPECT_EQ(t.c_lflag, kernel.c_lflag);
  EXPECT_EQ(t.c_line, kernel.c_line);
  EXPECT_EQ(t.c_ispeed, speed_t(B50));
  EXPECT_EQ(t.c_ospeed, speed_t(B115200));
  for (size_t i = 0; i < NCCS; ++i)
    EXPECT_EQ(t.c_cc[i], cc_t(i < LIBC_NAMESPACE::KERNEL_NCCS ? i + 1 : 0));
  kernel.c_cflag = B75;
  ASSERT_EQ(LIBC_NAMESPACE::tcgetattr(19, &t), 0);
  EXPECT_EQ(t.c_ispeed, speed_t(B75));
}

TEST_F(LlvmLibcTermiosConversionTest, SetAttributesAndActions) {
  termios t = {};
  t.c_iflag = ICRNL;
  t.c_oflag = OPOST;
  t.c_lflag = ISIG;
  t.c_line = 2;
  t.c_cflag = CS8 | CBAUD | CIBAUD;
  t.c_ispeed = B50;
  t.c_ospeed = B115200;
  for (size_t i = 0; i < NCCS; ++i)
    t.c_cc[i] = static_cast<cc_t>(i + 7);
  const int actions[] = {TCSANOW, TCSADRAIN, TCSAFLUSH};
  const unsigned long commands[] = {TCSETS, TCSETSW, TCSETSF};
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(LIBC_NAMESPACE::tcsetattr(21, actions[i], &t), 0);
    EXPECT_EQ(request, commands[i]);
    EXPECT_EQ(descriptor, 21);
    EXPECT_EQ(syscall_number, long(SYS_ioctl));
    EXPECT_EQ(kernel.c_iflag, t.c_iflag);
    EXPECT_EQ(kernel.c_oflag, t.c_oflag);
    EXPECT_EQ(kernel.c_lflag, t.c_lflag);
    EXPECT_EQ(kernel.c_line, t.c_line);
    EXPECT_EQ(kernel.c_cflag, tcflag_t(CS8 | B115200 | (B50 << 16)));
    for (size_t j = 0; j < LIBC_NAMESPACE::KERNEL_NCCS; ++j)
      EXPECT_EQ(kernel.c_cc[j], t.c_cc[j]);
  }
  EXPECT_EQ(calls, 3);
  t.c_ispeed = 0;
  ASSERT_EQ(LIBC_NAMESPACE::tcsetattr(21, TCSANOW, &t), 0);
  EXPECT_EQ(kernel.c_cflag, tcflag_t(CS8 | B115200 | (B115200 << 16)));
}

TEST_F(LlvmLibcTermiosConversionTest, Errors) {
  termios t = {};
  t.c_iflag = ICRNL;
  const int errors[] = {EBADF, ENOTTY, EINTR};
  for (int e : errors) {
    error = e;
    EXPECT_EQ(LIBC_NAMESPACE::tcgetattr(-1, &t), -1);
    EXPECT_EQ(int(libc_errno), e);
    EXPECT_EQ(t.c_iflag, tcflag_t(ICRNL));
    EXPECT_EQ(LIBC_NAMESPACE::tcsetattr(-1, TCSANOW, &t), -1);
    EXPECT_EQ(int(libc_errno), e);
  }
  calls = 0;
  EXPECT_EQ(LIBC_NAMESPACE::tcsetattr(1, -1, &t), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(calls, 0);
}

TEST_F(LlvmLibcTermiosConversionTest, SpeedHelpers) {
  const speed_t speeds[] = {
      B0,       B50,      B75,      B110,     B134,     B150,     B200,
      B300,     B600,     B1200,    B1800,    B2400,    B4800,    B9600,
      B19200,   B38400,   B57600,   B115200,  B230400,  B460800,  B500000,
      B576000,  B921600,  B1000000, B1152000, B1500000, B2000000, B2500000,
      B3000000, B3500000, B4000000};
  termios t = {};
  for (speed_t speed : speeds) {
    t.c_cflag = CS8 | CBAUD;
    ASSERT_EQ(LIBC_NAMESPACE::cfsetispeed(&t, speed), 0);
    ASSERT_EQ(LIBC_NAMESPACE::cfsetospeed(&t, speed), 0);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetispeed(&t), speed);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetospeed(&t), speed);
    EXPECT_EQ(t.c_cflag, tcflag_t(CS8 | speed));
  }
  const speed_t invalid[] = {speed_t(-1), 12345};
  for (speed_t speed : invalid) {
    EXPECT_EQ(LIBC_NAMESPACE::cfsetispeed(&t, speed), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(LIBC_NAMESPACE::cfsetospeed(&t, speed), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(t.c_ispeed, speed_t(B4000000));
    EXPECT_EQ(t.c_ospeed, speed_t(B4000000));
    EXPECT_EQ(t.c_cflag, tcflag_t(CS8 | B4000000));
  }
  EXPECT_EQ(calls, 0);
}
