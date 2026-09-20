//===-- Unittests for gethostname -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/gethostname.h"

#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"

using LlvmLibcGetHostNameTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST(LlvmLibcGetHostNameTest, GetCurrHostName) {
  char hostbuffer[1024];
  int ret = LIBC_NAMESPACE::gethostname(hostbuffer, sizeof(hostbuffer));
  ASSERT_NE(ret, -1);
  ASSERT_ERRNO_SUCCESS();

  ret = LIBC_NAMESPACE::gethostname(hostbuffer, 0);
  ASSERT_EQ(ret, -1);
  ASSERT_ERRNO_EQ(ENAMETOOLONG);

  // test for invalid pointer
  char *nptr = nullptr;
  ret = LIBC_NAMESPACE::gethostname(nptr, 1);
  ASSERT_EQ(ret, -1);
  ASSERT_ERRNO_EQ(EFAULT);
}

TEST(LlvmLibcGetHostNameTest, ZeroSizeDoesNotWrite) {
  char buffer[] = {'a', 'b', 'c'};
  ASSERT_EQ(LIBC_NAMESPACE::gethostname(buffer + 1, 0), -1);
  ASSERT_ERRNO_EQ(ENAMETOOLONG);
  EXPECT_EQ(buffer[0], 'a');
  EXPECT_EQ(buffer[1], 'b');
  EXPECT_EQ(buffer[2], 'c');
}

TEST(LlvmLibcGetHostNameTest, BufferBounds) {
  char hostname[1024];
  ASSERT_EQ(LIBC_NAMESPACE::gethostname(hostname, sizeof(hostname)), 0);
  ASSERT_ERRNO_SUCCESS();
  size_t length = 0;
  while (length < sizeof(hostname) && hostname[length] != '\0')
    ++length;
  ASSERT_LT(length, sizeof(hostname));

  // Exercise truncation, exact character fit, terminator fit, and spare space.
  char buffer[sizeof(hostname) + 3];
  for (size_t size = 1; size <= length + 2; ++size) {
    for (char &byte : buffer)
      byte = '#';
    libc_errno = 0;
    int ret = LIBC_NAMESPACE::gethostname(buffer + 1, size);
    if (size <= length) {
      ASSERT_EQ(ret, -1);
      ASSERT_ERRNO_EQ(ENAMETOOLONG);
    } else {
      ASSERT_EQ(ret, 0);
      ASSERT_ERRNO_SUCCESS();
    }
    size_t copied = length < size - 1 ? length : size - 1;
    EXPECT_EQ(buffer[0], '#');
    for (size_t i = 0; i < copied; ++i)
      EXPECT_EQ(buffer[i + 1], hostname[i]);
    EXPECT_EQ(buffer[copied + 1], '\0');
    for (size_t i = copied + 2; i < sizeof(buffer); ++i)
      EXPECT_EQ(buffer[i], '#');
  }
}
