//===-- Tests for termios speed setters -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/termios_macros.h"
#include "hdr/types/struct_termios.h"
#include "src/__support/libc_errno.h"
#include "src/termios/cfgetispeed.h"
#include "src/termios/cfgetospeed.h"
#include "src/termios/cfsetispeed.h"
#include "src/termios/cfsetospeed.h"
#include "test/UnitTest/Test.h"

#include <stddef.h>

namespace {

constexpr speed_t SPEEDS[] = {
    B0,   B50,   B75,   B110,  B134,  B150,  B200,   B300,
    B600, B1200, B1800, B2400, B4800, B9600, B19200, B38400,
#ifdef B57600
    B57600,
#endif
#if B50 == 1 && defined(B76800)
    B76800,
#endif
#ifdef B115200
    B115200,
#endif
#if B50 == 1 && defined(B153600)
    B153600,
#endif
#ifdef B230400
    B230400,
#endif
#if B50 == 1 && defined(B307200)
    B307200,
#endif
#ifdef B460800
    B460800,
#endif
#ifdef B500000
    B500000,
#endif
#ifdef B576000
    B576000,
#endif
#if B50 == 1 && defined(B614400)
    B614400,
#endif
#ifdef B921600
    B921600,
#endif
#ifdef B1000000
    B1000000,
#endif
#ifdef B1152000
    B1152000,
#endif
#ifdef B1500000
    B1500000,
#endif
#ifdef B2000000
    B2000000,
#endif
#ifdef B2500000
    B2500000,
#endif
#ifdef B3000000
    B3000000,
#endif
#ifdef B3500000
    B3500000,
#endif
#ifdef B4000000
    B4000000,
#endif
};

void expect_invalid_speed(speed_t speed) {
  int (*const setters[])(termios *, speed_t) = {
      LIBC_NAMESPACE::cfsetispeed, LIBC_NAMESPACE::cfsetospeed};
  for (auto setter : setters) {
    termios t;
    auto *bytes = reinterpret_cast<unsigned char *>(&t);
    // Initialize padding too, so rejection can check the entire object.
    for (size_t i = 0; i < sizeof(t); ++i)
      bytes[i] = static_cast<unsigned char>(i + 1);
    t.c_cflag = CLOCAL | CREAD | CS8 | B19200;
    t.c_ispeed = B9600;
    t.c_ospeed = B19200;
    unsigned char before[sizeof(t)];
    for (size_t i = 0; i < sizeof(t); ++i)
      before[i] = bytes[i];

    libc_errno = 0;
    EXPECT_EQ(setter(&t, speed), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    for (size_t i = 0; i < sizeof(t); ++i)
      EXPECT_EQ(bytes[i], before[i]);
  }
}

} // namespace

TEST(LlvmLibcTermiosSpeedTest, SupportedSpeeds) {
  for (speed_t speed : SPEEDS) {
    termios t{};
    t.c_cflag = CLOCAL | CREAD | CS8;
    ASSERT_EQ(LIBC_NAMESPACE::cfsetospeed(&t, B19200), 0);
    const tcflag_t old_flags = t.c_cflag;
    ASSERT_EQ(LIBC_NAMESPACE::cfsetispeed(&t, speed), 0);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetispeed(&t), speed);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetospeed(&t), speed_t(B19200));
    EXPECT_EQ(t.c_cflag, old_flags);

    ASSERT_EQ(LIBC_NAMESPACE::cfsetispeed(&t, B9600), 0);
    ASSERT_EQ(LIBC_NAMESPACE::cfsetospeed(&t, speed), 0);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetospeed(&t), speed);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetispeed(&t), speed_t(B9600));
    EXPECT_EQ(t.c_cflag & ~tcflag_t(CBAUD), old_flags & ~tcflag_t(CBAUD));
#if B50 == 1
    EXPECT_EQ(t.c_cflag & tcflag_t(CBAUD), tcflag_t(speed));
#endif
  }
}

TEST(LlvmLibcTermiosSpeedTest, InputZeroPreservesOutput) {
  termios t{};
  ASSERT_EQ(LIBC_NAMESPACE::cfsetospeed(&t, B75), 0);
  ASSERT_EQ(LIBC_NAMESPACE::cfsetispeed(&t, B50), 0);
  const tcflag_t old_flags = t.c_cflag;
  ASSERT_EQ(LIBC_NAMESPACE::cfsetispeed(&t, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::cfgetispeed(&t), speed_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::cfgetospeed(&t), speed_t(B75));
  EXPECT_EQ(t.c_cflag, old_flags);
}

TEST(LlvmLibcTermiosSpeedTest, InvalidSpeedPreservesObject) {
  expect_invalid_speed(speed_t(-1));
  expect_invalid_speed(speed_t(12345));
#if B50 != 1
  // Kernel encodings are not public numerical baud rates in overlay mode.
  expect_invalid_speed(speed_t(1));
  expect_invalid_speed(speed_t(4096));
#else
  // Check every unnamed encoding within CBAUD, including the BOTHER slot.
  // Do not assume that extended rates use the asm-generic numbering.
  for (speed_t speed = 0; speed <= speed_t(CBAUD); ++speed) {
    if ((speed & ~speed_t(CBAUD)) != 0)
      continue;
    bool named = false;
    for (speed_t supported : SPEEDS)
      named |= speed == supported;
    if (!named)
      expect_invalid_speed(speed);
  }
#endif
}
