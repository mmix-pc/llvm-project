//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unittests for confstr
///
//===----------------------------------------------------------------------===//

#include "src/unistd/confstr.h"

#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "hdr/unistd_macros.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"

using LlvmLibcConfStrTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;

TEST_F(LlvmLibcConfStrTest, InvalidName) {
  char buf[64] = "initial";
  EXPECT_THAT(LIBC_NAMESPACE::confstr(-1, buf, sizeof(buf)),
              Fails(EINVAL, size_t(0)));
  EXPECT_STREQ(buf, "initial");
  EXPECT_THAT(LIBC_NAMESPACE::confstr(-1, nullptr, 0),
              Fails(EINVAL, size_t(0)));
}

TEST_F(LlvmLibcConfStrTest, PathAndTruncation) {
  const char expected[] = "/usr/bin:/bin";
  char buf[32] = "initial";
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_PATH, nullptr, 0), sizeof(expected));
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_PATH, buf, 0), sizeof(expected));
  EXPECT_STREQ(buf, "initial");
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_PATH, buf, sizeof(buf)),
            sizeof(expected));
  EXPECT_STREQ(buf, expected);
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_PATH, buf, 5), sizeof(expected));
  EXPECT_STREQ(buf, "/usr");
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_PATH, buf, 1), sizeof(expected));
  EXPECT_EQ(buf[0], '\0');
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = 0;
}

TEST_F(LlvmLibcConfStrTest, UnsupportedEnvironmentPreservesErrno) {
  char buf[] = "unchanged";
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_V7_ENV, buf, sizeof(buf)), size_t(0));
  EXPECT_STREQ(buf, "unchanged");
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = 0;
}

TEST_F(LlvmLibcConfStrTest, NativeLP64) {
  if (sizeof(int) != 4 || sizeof(long) != 8 || sizeof(void *) != 8)
    return;
  char buf[64];
  EXPECT_EQ(
      LIBC_NAMESPACE::confstr(_CS_POSIX_V7_LP64_OFF64_CFLAGS, buf, sizeof(buf)),
      size_t(1));
  EXPECT_STREQ(buf, "");
  EXPECT_EQ(
      LIBC_NAMESPACE::confstr(_CS_V7_WIDTH_RESTRICTED_ENVS, buf, sizeof(buf)),
      sizeof("POSIX_V7_LP64_OFF64"));
  EXPECT_STREQ(buf, "POSIX_V7_LP64_OFF64");
}

#ifdef _CS_POSIX_V8_THREADS_CFLAGS
TEST_F(LlvmLibcConfStrTest, ThreadFlagsAndNewModelQueries) {
  char buf[64];
  libc_errno = EDOM;
  size_t expected = _POSIX_THREADS > 0 ? sizeof("-pthread") : 0;
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_POSIX_V8_THREADS_CFLAGS, buf,
                                   sizeof(buf)),
            expected);
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_POSIX_V7_THREADS_LDFLAGS, buf,
                                   sizeof(buf)),
            expected);
  if (expected)
    EXPECT_STREQ(buf, "-pthread");
  EXPECT_EQ(LIBC_NAMESPACE::confstr(_CS_POSIX_V8_ILP32_OFF32_CFLAGS, buf,
                                   sizeof(buf)),
            size_t(0));
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = 0;
}
#endif
