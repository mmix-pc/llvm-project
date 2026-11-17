//===----------------------------------------------------------------------===//
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
#include "src/termios/cfmakeraw.h"
#include "src/termios/cfsetspeed.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcTermiosCompatibilityTest, RawModePreservesUnrelatedState) {
  termios t{};
  t.c_iflag = IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON |
              IXOFF | INPCK;
  t.c_oflag = OPOST | ONLCR;
  t.c_lflag = ECHO | ECHONL | ICANON | ISIG | IEXTEN | NOFLSH | TOSTOP;
  t.c_cflag = CS7 | PARENB | CREAD | CLOCAL | CSTOPB;
  t.c_ispeed = B9600;
  t.c_ospeed = B38400;
  for (unsigned i = 0; i < NCCS; ++i)
    t.c_cc[i] = static_cast<cc_t>(i + 10);
  libc_errno = EDOM;
  LIBC_NAMESPACE::cfmakeraw(&t);
  EXPECT_EQ(t.c_iflag, static_cast<tcflag_t>(IXOFF | INPCK));
  EXPECT_EQ(t.c_oflag, static_cast<tcflag_t>(ONLCR));
  EXPECT_EQ(t.c_lflag, static_cast<tcflag_t>(NOFLSH | TOSTOP));
  EXPECT_EQ(t.c_cflag, static_cast<tcflag_t>(CS8 | CREAD | CLOCAL | CSTOPB));
  EXPECT_EQ(t.c_ispeed, static_cast<speed_t>(B9600));
  EXPECT_EQ(t.c_ospeed, static_cast<speed_t>(B38400));
  for (unsigned i = 0; i < NCCS; ++i)
    EXPECT_EQ(t.c_cc[i], static_cast<cc_t>(i == VMIN    ? 1
                                           : i == VTIME ? 0
                                                        : i + 10));
  EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
  termios once = t;
  LIBC_NAMESPACE::cfmakeraw(&t);
  EXPECT_EQ(t.c_iflag, once.c_iflag);
  EXPECT_EQ(t.c_oflag, once.c_oflag);
  EXPECT_EQ(t.c_lflag, once.c_lflag);
  EXPECT_EQ(t.c_cflag, once.c_cflag);
}

TEST(LlvmLibcTermiosCompatibilityTest, BothSpeedsAndInvalidNonmutation) {
  termios t{};
  t.c_lflag = ICANON | ECHO;
  t.c_cflag = CLOCAL | CREAD | CS8;
  t.c_cc[VINTR] = 3;
  constexpr speed_t speeds[] = {B9600, B0, B115200};
  for (speed_t speed : speeds) {
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::cfsetspeed(&t, speed), 0);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetispeed(&t), speed);
    EXPECT_EQ(LIBC_NAMESPACE::cfgetospeed(&t), speed);
    EXPECT_EQ(t.c_lflag, static_cast<tcflag_t>(ICANON | ECHO));
    EXPECT_EQ(t.c_cflag & (CLOCAL | CREAD | CSIZE),
              static_cast<tcflag_t>(CLOCAL | CREAD | CS8));
    EXPECT_EQ(t.c_cc[VINTR], static_cast<cc_t>(3));
    EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
  }
  unsigned char before[sizeof(t)];
  __builtin_memcpy(before, &t, sizeof(t));
  EXPECT_EQ(LIBC_NAMESPACE::cfsetspeed(&t, ~static_cast<speed_t>(0)), -1);
  EXPECT_EQ(static_cast<int>(libc_errno), EINVAL);
  EXPECT_EQ(__builtin_memcmp(before, &t, sizeof(t)), 0);
}
