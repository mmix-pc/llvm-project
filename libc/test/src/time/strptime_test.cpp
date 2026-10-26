//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains unit tests for strptime.
///
//===----------------------------------------------------------------------===//

#include "hdr/types/struct_tm.h"
#include "src/time/strptime.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcStrptimeTest, FieldsAndProgress) {
  tm time{};
  time.tm_yday = 123;
  time.tm_wday = 6;
  time.tm_isdst = -1;
  char input[] = "2024-02-29 13:23:60 remainder";
  ASSERT_EQ(LIBC_NAMESPACE::strptime(input, "%F %T", &time), input + 19);
  EXPECT_EQ(time.tm_year, 124);
  EXPECT_EQ(time.tm_mon, 1);
  EXPECT_EQ(time.tm_mday, 29);
  EXPECT_EQ(time.tm_hour, 13);
  EXPECT_EQ(time.tm_min, 23);
  EXPECT_EQ(time.tm_sec, 60);
  EXPECT_EQ(time.tm_yday, 123);
  EXPECT_EQ(time.tm_wday, 6);
  EXPECT_EQ(time.tm_isdst, -1);
}

TEST(LlvmLibcStrptimeTest, NamesAndCompositeFormats) {
  tm time{};
  ASSERT_NE(LIBC_NAMESPACE::strptime("tHURsday fEBRUARY 29", "%a %b %d", &time),
            nullptr);
  EXPECT_EQ(time.tm_wday, 4);
  EXPECT_EQ(time.tm_mon, 1);
  EXPECT_EQ(time.tm_mday, 29);
  ASSERT_NE(LIBC_NAMESPACE::strptime("Sun Mar 3 12:34:56 2024", "%c", &time),
            nullptr);
  EXPECT_EQ(time.tm_year, 124);
  EXPECT_EQ(time.tm_mon, 2);
  EXPECT_EQ(time.tm_sec, 56);
  ASSERT_NE(LIBC_NAMESPACE::strptime("12/31/68 11:59:12 pm", "%x %r", &time),
            nullptr);
  EXPECT_EQ(time.tm_year, 168);
  EXPECT_EQ(time.tm_hour, 23);
}

TEST(LlvmLibcStrptimeTest, CombinedFieldsInEitherOrder) {
  tm time{};
  ASSERT_NE(LIBC_NAMESPACE::strptime("19 04", "%C %y", &time), nullptr);
  EXPECT_EQ(time.tm_year, 4);
  ASSERT_NE(LIBC_NAMESPACE::strptime("04 19", "%y %C", &time), nullptr);
  EXPECT_EQ(time.tm_year, 4);
  ASSERT_NE(LIBC_NAMESPACE::strptime("19", "%C", &time), nullptr);
  EXPECT_EQ(time.tm_year, 0);
  ASSERT_NE(LIBC_NAMESPACE::strptime("69", "%y", &time), nullptr);
  EXPECT_EQ(time.tm_year, 69);
  ASSERT_NE(LIBC_NAMESPACE::strptime("pm 12", "%p %I", &time), nullptr);
  EXPECT_EQ(time.tm_hour, 12);
  ASSERT_NE(LIBC_NAMESPACE::strptime("12 am", "%I %p", &time), nullptr);
  EXPECT_EQ(time.tm_hour, 0);
}

TEST(LlvmLibcStrptimeTest, WidthsSignsAndLargeYears) {
  tm time{};
  char input[] = "12345-1-2!";
  ASSERT_EQ(LIBC_NAMESPACE::strptime(input, "%F", &time), input + 9);
  EXPECT_EQ(time.tm_year, 10445);
  EXPECT_EQ(LIBC_NAMESPACE::strptime(input, "%7F", &time), nullptr);
  ASSERT_NE(LIBC_NAMESPACE::strptime("+12345", "%+6Y", &time), nullptr);
  EXPECT_EQ(time.tm_year, 10445);
  ASSERT_NE(LIBC_NAMESPACE::strptime("-001", "%04Y", &time), nullptr);
  EXPECT_EQ(time.tm_year, -1901);
  ASSERT_NE(LIBC_NAMESPACE::strptime("2147485547", "%10Y", &time), nullptr);
  EXPECT_EQ(time.tm_year, 2147483647);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("2147485548", "%10Y", &time), nullptr);
  ASSERT_NE(LIBC_NAMESPACE::strptime("-2147481748", "%11Y", &time), nullptr);
  EXPECT_EQ(time.tm_year, -2147483647 - 1);
  EXPECT_EQ(
      LIBC_NAMESPACE::strptime("1", "%99999999999999999999999999Y", &time),
      nullptr);
}

TEST(LlvmLibcStrptimeTest, WhitespaceAndNumericRanges) {
  tm time{};
  char input[] = " \t366 \n7%tail";
  ASSERT_EQ(LIBC_NAMESPACE::strptime(input, "%n%j%t%u%%", &time), input + 9);
  EXPECT_EQ(time.tm_yday, 365);
  EXPECT_EQ(time.tm_wday, 0);
  const char *invalid[] = {"00", "13", "24", "00", "60", "61", "367", "7"};
  const char *formats[] = {"%d", "%m", "%H", "%I", "%M", "%S", "%j", "%w"};
  for (unsigned i = 0; i != 8; ++i)
    EXPECT_EQ(LIBC_NAMESPACE::strptime(invalid[i], formats[i], &time), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("", "%Y", &time), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("12", "%", &time), nullptr);
  char empty[] = "tail";
  EXPECT_EQ(LIBC_NAMESPACE::strptime(empty, " %t%n", &time), empty);
}

TEST(LlvmLibcStrptimeTest, FailurePreservesFields) {
  tm time{};
  time.tm_year = 77;
  time.tm_mon = 8;
  EXPECT_EQ(LIBC_NAMESPACE::strptime("2024-13-01", "%F", &time), nullptr);
  EXPECT_EQ(time.tm_year, 77);
  EXPECT_EQ(time.tm_mon, 8);
}

TEST(LlvmLibcStrptimeTest, UnspecifiedWeekEpochAndOffsetEffects) {
  tm time{};
  time.tm_year = 77;
  time.tm_mday = 2;
  ASSERT_NE(LIBC_NAMESPACE::strptime("2024 24 53 00 53 -123 -0430",
                                     "%G %g %V %U %W %s %z", &time),
            nullptr);
  EXPECT_EQ(time.tm_year, 77);
  EXPECT_EQ(time.tm_mday, 2);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("00", "%V", &time), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("+2400", "%z", &time), nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("+12", "%z", &time), nullptr);
  char expanded[] = "12345!";
  EXPECT_EQ(LIBC_NAMESPACE::strptime(expanded, "%G", &time), expanded + 5);
  EXPECT_EQ(time.tm_year, 77);
  ASSERT_NE(LIBC_NAMESPACE::strptime("UTC", "%Z", &time), nullptr);
  EXPECT_EQ(time.tm_isdst, 0);
}
