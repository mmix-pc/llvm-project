//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/linux/termios-macros.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcTermiosMacrosTest, LinuxExtendedFlags) {
  EXPECT_EQ(CBAUDEX, 0x1000);
  EXPECT_EQ(CBAUDEX, CBAUDX);
  EXPECT_EQ(CBAUD & CBAUDEX, CBAUDEX);
  EXPECT_EQ(ECHOCTL, 0x0200);
  EXPECT_EQ(ECHOKE, 0x0800);
  EXPECT_EQ(FLUSHO, 0x1000);
  EXPECT_EQ(IEXTEN, 0x8000);
}

TEST(LlvmLibcTermiosMacrosTest, LinuxFlushSelectors) {
  EXPECT_EQ(TCIFLUSH, 0);
  EXPECT_EQ(TCOFLUSH, 1);
  EXPECT_EQ(TCIOFLUSH, 2);
}
