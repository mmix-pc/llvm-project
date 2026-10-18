//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit test for tzset.
///
//===----------------------------------------------------------------------===//

#include "src/time/tz_variables.h"
#include "src/time/tzset.h"
#include "test/UnitTest/Test.h"
#include "test/src/time/TimezoneGuard.h"

TEST(LlvmLibcTzsetTest, UnsetAndEmptyUseUtc) {
  LIBC_NAMESPACE::testing::TimezoneGuard zone;
  ASSERT_TRUE(zone.set(nullptr));
  LIBC_NAMESPACE::tzset();

  EXPECT_STREQ(LIBC_NAMESPACE::tzname[0], "UTC");
  EXPECT_STREQ(LIBC_NAMESPACE::tzname[1], "");
  EXPECT_EQ(LIBC_NAMESPACE::timezone, 0L);
  EXPECT_EQ(LIBC_NAMESPACE::daylight, 0);
  ASSERT_TRUE(zone.set(""));
  LIBC_NAMESPACE::tzset();
  EXPECT_STREQ(LIBC_NAMESPACE::tzname[0], "UTC");
}

TEST(LlvmLibcTzsetTest, BuiltinZeroOffsetNames) {
  LIBC_NAMESPACE::testing::TimezoneGuard zone;
  const char *names[] = {"GMT", "UTC"};
  for (const char *name : names) {
    ASSERT_TRUE(zone.set(name));
    LIBC_NAMESPACE::tzset();
    EXPECT_STREQ(LIBC_NAMESPACE::tzname[0], name);
    EXPECT_EQ(LIBC_NAMESPACE::timezone, 0L);
    EXPECT_EQ(LIBC_NAMESPACE::daylight, 0);
  }
}
