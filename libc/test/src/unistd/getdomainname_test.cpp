//===-- Unittests for getdomainname
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/sys/utsname/uname.h"
#include "src/unistd/getdomainname.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcGetDomainNameTest, UnameAndBoundedTruncation) {
  utsname info;
  ASSERT_GE(LIBC_NAMESPACE::uname(&info), 0);
  unsigned length = 0;
  while (info.domainname[length])
    ++length;
  for (unsigned size = 0; size <= length + 2; ++size) {
    char buffer[sizeof(info.domainname) + 3];
    for (char &c : buffer)
      c = '#';
    libc_errno = EDOM;
    ASSERT_EQ(LIBC_NAMESPACE::getdomainname(buffer, size), 0);
    unsigned copied = size < length + 1 ? size : length + 1;
    for (unsigned i = 0; i < sizeof(buffer); ++i)
      EXPECT_EQ(buffer[i], i < copied ? info.domainname[i] : '#');
    EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
  }
  EXPECT_EQ(LIBC_NAMESPACE::getdomainname(nullptr, 0), 0);
  EXPECT_EQ(LIBC_NAMESPACE::getdomainname(nullptr, 1), -1);
  EXPECT_EQ(static_cast<int>(libc_errno), EFAULT);
}
