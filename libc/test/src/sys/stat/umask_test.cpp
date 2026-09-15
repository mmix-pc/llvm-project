//===-- Unittests for umask -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/sys/stat/umask.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcUmaskTest, SetAndReturnPreviousMask) {
  mode_t original = LIBC_NAMESPACE::umask(0022);
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::umask(0077), mode_t(0022));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::umask(0), mode_t(0077));
  EXPECT_EQ(LIBC_NAMESPACE::umask(original), mode_t(0));
}

TEST(LlvmLibcUmaskTest, IgnoreBitsOutsidePermissions) {
  mode_t original = LIBC_NAMESPACE::umask(mode_t(-1));
  EXPECT_EQ(LIBC_NAMESPACE::umask(0), mode_t(0777));
  LIBC_NAMESPACE::umask(original);
}
