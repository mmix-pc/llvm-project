//===-- Unittests for getentropy ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/unistd/getentropy.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::testing::ErrnoSetterMatcher;
using LlvmLibcUnistdGetEntropyTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcUnistdGetEntropyTest, LengthTooLong) {
  char buf[257];
  for (char &byte : buf)
    byte = 42;
  ASSERT_THAT(LIBC_NAMESPACE::getentropy(buf, sizeof(buf)), Fails(EINVAL));
  for (char byte : buf)
    EXPECT_EQ(byte, char(42));
  ASSERT_THAT(LIBC_NAMESPACE::getentropy(nullptr, 257), Fails(EINVAL));
  ASSERT_THAT(LIBC_NAMESPACE::getentropy(nullptr, size_t(-1)), Fails(EINVAL));
}

TEST_F(LlvmLibcUnistdGetEntropyTest, ZeroLength) {
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::getentropy(nullptr, 0), 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  LIBC_NAMESPACE::libc_errno = 0;
}

TEST_F(LlvmLibcUnistdGetEntropyTest, SmokeTest) {
  char buf[256];
  ASSERT_THAT(LIBC_NAMESPACE::getentropy(buf, 256), Succeeds());
}

TEST_F(LlvmLibcUnistdGetEntropyTest, OtherError) {
  ASSERT_THAT(LIBC_NAMESPACE::getentropy(nullptr, 1), Fails(EIO));
}
