//===-- POSIX TZ parser and transition tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/posix_timezone.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::time_utils::parse_posix_timezone;
using LIBC_NAMESPACE::time_utils::PosixTimeZone;
using LIBC_NAMESPACE::time_utils::TZParseResult;

TEST(LlvmLibcPosixTimezoneTest, OffsetsAndNames) {
  PosixTimeZone zone;
  ASSERT_TRUE(parse_posix_timezone("<+054530>-5:45:30", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_TRUE(zone.names[0] == "+054530");
  EXPECT_EQ(zone.offsets[0], 20730);
  EXPECT_FALSE(zone.has_dst);
  ASSERT_TRUE(parse_posix_timezone("STD+24:59:59DST-24:59:59,J60,J300", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_EQ(zone.offsets[0], -89999);
  EXPECT_EQ(zone.offsets[1], 89999);
  ASSERT_TRUE(parse_posix_timezone("EST5EDT,M3.2.0,M11.1.0", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_EQ(zone.offsets[1], -14400);
}

TEST(LlvmLibcPosixTimezoneTest, InvalidAndUnsupportedAreTransactional) {
  PosixTimeZone zone;
  ASSERT_TRUE(parse_posix_timezone("UTC0", zone) == TZParseResult::SUCCESS);
  const char *invalid[] = {"",
                           "AB0",
                           "<>0",
                           "<AB!>0",
                           "UTC25",
                           "UTC1:60",
                           "UTC1:2:60",
                           "UTC-",
                           "UTC0,",
                           "UTC0junk!",
                           "STD0DST,J0,J365",
                           "STD0DST,366,1",
                           "STD0DST,M0.1.0,M11.1.0",
                           "STD0DST,M3.0.0,M11.1.0",
                           "STD0DST,M3.6.0,M11.1.0",
                           "STD0DST,M3.1.7,M11.1.0",
                           "STD0DST,J60/168,J300",
                           "STD0DST,J60/-168,J300",
                           "STD0DST,J60/2:60,J300",
                           "STD0DST,J60,J300suffix",
                           "UTC999999999999999999999999999999"};
  for (const char *text : invalid) {
    EXPECT_TRUE(parse_posix_timezone(text, zone) == TZParseResult::INVALID);
    EXPECT_TRUE(zone.names[0] == "UTC");
    EXPECT_FALSE(zone.has_dst);
  }
  EXPECT_TRUE(parse_posix_timezone(":UTC0", zone) ==
              TZParseResult::UNSUPPORTED);
  EXPECT_TRUE(parse_posix_timezone("Europe/Paris", zone) ==
              TZParseResult::UNSUPPORTED);
  EXPECT_TRUE(parse_posix_timezone("EST5EDT", zone) ==
              TZParseResult::MISSING_RULES);
  EXPECT_TRUE(parse_posix_timezone("EST5EDT4", zone) ==
              TZParseResult::MISSING_RULES);
}

TEST(LlvmLibcPosixTimezoneTest, NorthernAndSouthernBoundaries) {
  PosixTimeZone zone;
  ASSERT_TRUE(parse_posix_timezone("EST5EDT,M3.2.0,M11.1.0", zone) ==
              TZParseResult::SUCCESS);
  // 2024-03-10 07:00 UTC and 2024-11-03 06:00 UTC.
  EXPECT_FALSE(zone.is_dst(1710053999, 2024));
  EXPECT_TRUE(zone.is_dst(1710054000, 2024));
  EXPECT_TRUE(zone.is_dst(1730613599, 2024));
  EXPECT_FALSE(zone.is_dst(1730613600, 2024));
  ASSERT_TRUE(parse_posix_timezone("AEST-10AEDT-11,M10.1.0,M4.1.0/3", zone) ==
              TZParseResult::SUCCESS);
  // 2024-04-06 16:00 UTC and 2024-10-05 16:00 UTC.
  EXPECT_TRUE(zone.is_dst(1712419199, 2024));
  EXPECT_FALSE(zone.is_dst(1712419200, 2024));
  EXPECT_FALSE(zone.is_dst(1728143999, 2024));
  EXPECT_TRUE(zone.is_dst(1728144000, 2024));
}

TEST(LlvmLibcPosixTimezoneTest, JulianLeapDaysAndLastWeek) {
  PosixTimeZone zone;
  ASSERT_TRUE(parse_posix_timezone("STD0DST,J60/0,J300/0", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_EQ(zone.start.local_seconds(2024), int64_t(1709251200)); // March 1
  EXPECT_EQ(zone.start.local_seconds(2023), int64_t(1677628800));
  ASSERT_TRUE(parse_posix_timezone("STD0DST,59/0,300/0", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_EQ(zone.start.local_seconds(2024), int64_t(1709164800)); // February 29
  EXPECT_EQ(zone.start.local_seconds(2023), int64_t(1677628800));
  ASSERT_TRUE(parse_posix_timezone("STD0DST,M2.5.0/0,M3.5.0/0", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_EQ(zone.start.local_seconds(2024), int64_t(1708819200)); // February 25
  EXPECT_EQ(zone.end.local_seconds(2024), int64_t(1711843200));   // March 31
}

TEST(LlvmLibcPosixTimezoneTest, DisplacedTransitionsAndPermanentDaylight) {
  PosixTimeZone zone;
  ASSERT_TRUE(parse_posix_timezone("STD0DST,0/-2,J200/167:59:59", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_EQ(zone.start.local_seconds(2024), int64_t(1704060000));
  EXPECT_FALSE(zone.is_dst(1704059999, 2023));
  EXPECT_TRUE(zone.is_dst(1704060000, 2023));
  ASSERT_TRUE(parse_posix_timezone("EST5EDT,0/0,J365/25", zone) ==
              TZParseResult::SUCCESS);
  EXPECT_TRUE(zone.is_dst(1704085199, 2024));
  EXPECT_TRUE(zone.is_dst(1704085200, 2024));
  EXPECT_TRUE(zone.is_dst(1735689600, 2025));
  ASSERT_TRUE(parse_posix_timezone("STD0DST,J364/167,J365/167", zone) ==
              TZParseResult::SUCCESS);
  // Both previous-year transitions are still in the future on January 1.
  EXPECT_FALSE(zone.is_dst(1704067200, 2024));
}

TEST(LlvmLibcPosixTimezoneTest, ProlepticCalendar) {
  using LIBC_NAMESPACE::time_utils::civil_days;
  EXPECT_EQ(civil_days(1970, 1, 1), int64_t(0));
  EXPECT_EQ(civil_days(1969, 12, 31), int64_t(-1));
  EXPECT_EQ(civil_days(0, 3, 1) - civil_days(0, 2, 28), int64_t(2));
  EXPECT_EQ(civil_days(-400, 3, 1) - civil_days(-400, 2, 28), int64_t(2));
  EXPECT_EQ(civil_days(-100, 3, 1) - civil_days(-100, 2, 28), int64_t(1));
}
