//===-- POSIX timezone public API tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "src/time/ctime_r.h"
#include "src/time/gmtime_r.h"
#include "src/time/localtime_r.h"
#include "src/time/mktime.h"
#include "src/time/strftime.h"
#include "src/time/time_utils.h"
#include "src/time/tz_variables.h"
#include "src/time/tzset.h"
#include "test/UnitTest/Test.h"
#include "test/src/time/TimezoneGuard.h"

namespace {
using namespace LIBC_NAMESPACE;

class LlvmLibcTimezoneTest : public testing::Test {
protected:
  testing::TimezoneGuard zone{false};
  void SetUp() override { zone.save(); }
  void TearDown() override { zone.restore(); }
  static tm calendar(int year, int month, int day, int hour, int minute,
                     int is_dst) {
    tm result{};
    result.tm_year = year - 1900;
    result.tm_mon = month - 1;
    result.tm_mday = day;
    result.tm_hour = hour;
    result.tm_min = minute;
    result.tm_isdst = is_dst;
    return result;
  }
};

TEST_F(LlvmLibcTimezoneTest, FixedOffsetsAndUtc) {
  ASSERT_TRUE(zone.set("<+054530>-5:45:30"));
  time_t epoch = 0;
  tm local{};
  ASSERT_EQ(LIBC_NAMESPACE::localtime_r(&epoch, &local), &local);
  EXPECT_EQ(local.tm_hour, 5);
  EXPECT_EQ(local.tm_min, 45);
  EXPECT_EQ(local.tm_sec, 30);
  EXPECT_EQ(local.tm_gmtoff, 20730L);
  EXPECT_EQ(local.tm_isdst, 0);
  EXPECT_EQ(LIBC_NAMESPACE::timezone, -20730L);
  EXPECT_EQ(LIBC_NAMESPACE::mktime(&local), epoch);
  char buffer[96];
  EXPECT_NE(
      LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%z %Z %s", &local),
      size_t(0));
  EXPECT_STREQ(buffer, "+0545 +054530 0");
  EXPECT_EQ(LIBC_NAMESPACE::ctime_r(&epoch, buffer), buffer);
  EXPECT_STREQ(buffer, "Thu Jan  1 05:45:30 1970\n");
  ASSERT_EQ(LIBC_NAMESPACE::gmtime_r(&epoch, &local), &local);
  EXPECT_EQ(local.tm_gmtoff, 0L);
  EXPECT_NE(
      LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%z %Z %s", &local),
      size_t(0));
  EXPECT_STREQ(buffer, "+0000 GMT 0");
}

TEST_F(LlvmLibcTimezoneTest, TransitionRoundTripsAndFormatting) {
  ASSERT_TRUE(zone.set("EST5EDT,M3.2.0,M11.1.0"));
  LIBC_NAMESPACE::tzset();
  EXPECT_STREQ(LIBC_NAMESPACE::tzname[0], "EST");
  EXPECT_STREQ(LIBC_NAMESPACE::tzname[1], "EDT");
  EXPECT_EQ(LIBC_NAMESPACE::timezone, 18000L);
  EXPECT_EQ(LIBC_NAMESPACE::daylight, 1);
  const time_t instants[] = {-1,         0,          1710053999,
                             1710054000, 1730613599, 1730613600};
  for (time_t instant : instants) {
    tm local{};
    ASSERT_EQ(LIBC_NAMESPACE::localtime_r(&instant, &local), &local);
    EXPECT_EQ(LIBC_NAMESPACE::mktime(&local), instant);
  }
  time_t spring = 1710054000;
  tm local{};
  ASSERT_EQ(LIBC_NAMESPACE::localtime_r(&spring, &local), &local);
  EXPECT_EQ(local.tm_hour, 3);
  EXPECT_EQ(local.tm_isdst, 1);
  char buffer[96];
  EXPECT_NE(LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%F %T %z %Z %s",
                                     &local),
            size_t(0));
  EXPECT_STREQ(buffer, "2024-03-10 03:00:00 -0400 EDT 1710054000");
  tm manual = calendar(2024, 7, 1, 12, 0, 1);
  EXPECT_NE(LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%z %Z", &manual),
            size_t(0));
  EXPECT_STREQ(buffer, "-0400 EDT");
  manual.tm_isdst = -1;
  EXPECT_EQ(LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%z%Z", &manual),
            size_t(0));
  EXPECT_STREQ(buffer, "");
}

TEST_F(LlvmLibcTimezoneTest, FoldGapAndExplicitHints) {
  ASSERT_TRUE(zone.set("EST5EDT,M3.2.0,M11.1.0"));
  tm folded = calendar(2024, 11, 3, 1, 30, -1);
  EXPECT_EQ(LIBC_NAMESPACE::mktime(&folded), time_t(1730615400));
  EXPECT_EQ(folded.tm_isdst, 0);
  folded = calendar(2024, 11, 3, 1, 30, 1);
  EXPECT_EQ(LIBC_NAMESPACE::mktime(&folded), time_t(1730611800));
  EXPECT_EQ(folded.tm_isdst, 1);
  tm gap = calendar(2024, 3, 10, 2, 30, -1);
  EXPECT_EQ(LIBC_NAMESPACE::mktime(&gap), time_t(1710055800));
  EXPECT_EQ(gap.tm_hour, 3);
  EXPECT_EQ(gap.tm_isdst, 1);
  gap = calendar(2024, 3, 10, 2, 30, 1);
  EXPECT_EQ(LIBC_NAMESPACE::mktime(&gap), time_t(1710052200));
  EXPECT_EQ(gap.tm_hour, 1);
  EXPECT_EQ(gap.tm_isdst, 0);
  tm summer = calendar(2024, 7, 1, 12, 0, 0);
  EXPECT_NE(LIBC_NAMESPACE::mktime(&summer), time_t(-1));
  EXPECT_EQ(summer.tm_hour, 13);
  EXPECT_EQ(summer.tm_isdst, 1);
}

TEST_F(LlvmLibcTimezoneTest, SouthernAndNegativeDaylight) {
  ASSERT_TRUE(zone.set("AEST-10AEDT-11,M10.1.0,M4.1.0/3"));
  const time_t instants[] = {1704067200, 1712419199, 1712419200, 1728143999,
                             1728144000};
  const int dst[] = {1, 1, 0, 0, 1};
  for (unsigned i = 0; i < 5; ++i) {
    tm local{};
    ASSERT_EQ(LIBC_NAMESPACE::localtime_r(&instants[i], &local), &local);
    EXPECT_EQ(local.tm_isdst, dst[i]);
    EXPECT_EQ(local.tm_gmtoff, dst[i] ? 39600L : 36000L);
    EXPECT_EQ(LIBC_NAMESPACE::mktime(&local), instants[i]);
  }
  ASSERT_TRUE(zone.set("STD0DST1,M3.2.0,M11.1.0"));
  tm gap = calendar(2024, 11, 3, 2, 30, -1);
  EXPECT_NE(LIBC_NAMESPACE::mktime(&gap), time_t(-1));
  EXPECT_EQ(gap.tm_hour, 3);
  EXPECT_EQ(gap.tm_isdst, 0);
}

TEST_F(LlvmLibcTimezoneTest, InvalidUnsupportedAndRecovery) {
  const char *values[] = {"UTC25", "bad!", "Europe/Paris", ":UTC0", "EST5EDT"};
  const int errors[] = {EINVAL, EINVAL, ENOTSUP, ENOTSUP, ENOTSUP};
  time_t epoch = 0;
  for (unsigned i = 0; i < 5; ++i) {
    ASSERT_TRUE(zone.set(values[i]));
    libc_errno = 0;
    LIBC_NAMESPACE::tzset();
    EXPECT_EQ(static_cast<int>(libc_errno), errors[i]);
    EXPECT_STREQ(LIBC_NAMESPACE::tzname[0], "");
    tm local = calendar(1970, 1, 1, 0, 0, -1);
    EXPECT_EQ(LIBC_NAMESPACE::localtime_r(&epoch, &local), nullptr);
    EXPECT_EQ(static_cast<int>(libc_errno), errors[i]);
    EXPECT_EQ(LIBC_NAMESPACE::mktime(&local), time_t(-1));
    EXPECT_EQ(static_cast<int>(libc_errno), errors[i]);
    EXPECT_EQ(local.tm_year, 70);
    char buffer[32];
    EXPECT_EQ(LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%s", &local),
              size_t(0));
    EXPECT_EQ(LIBC_NAMESPACE::gmtime_r(&epoch, &local), &local);
  }
  ASSERT_TRUE(zone.set("UTC0"));
  tm local{};
  EXPECT_EQ(LIBC_NAMESPACE::localtime_r(&epoch, &local), &local);
  EXPECT_EQ(local.tm_hour, 0);
}

TEST_F(LlvmLibcTimezoneTest, AbbreviationLifetimeAndRanges) {
  ASSERT_TRUE(zone.set("ABC-1"));
  time_t epoch = 0;
  tm old{};
  ASSERT_EQ(LIBC_NAMESPACE::localtime_r(&epoch, &old), &old);
  const char *name = old.tm_zone;
  ASSERT_TRUE(zone.set("XYZ3"));
  LIBC_NAMESPACE::tzset();
  EXPECT_STREQ(name, "ABC");
  char buffer[32];
  EXPECT_NE(LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%z %Z %s", &old),
            size_t(0));
  EXPECT_STREQ(buffer, "+0100 ABC 0");
  ASSERT_TRUE(zone.set("UTC0"));
  time_t minus_one = -1;
  ASSERT_EQ(LIBC_NAMESPACE::localtime_r(&minus_one, &old), &old);
  libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::mktime(&old), minus_one);
  EXPECT_EQ(static_cast<int>(libc_errno), 0);
  if constexpr (sizeof(time_t) == 8) {
    time_t maximum = cpp::numeric_limits<time_t>::max();
    EXPECT_EQ(LIBC_NAMESPACE::localtime_r(&maximum, &old), nullptr);
    EXPECT_EQ(static_cast<int>(libc_errno), time_utils::TIME_OVERFLOW);
    const int years[] = {cpp::numeric_limits<int>::min(),
                         cpp::numeric_limits<int>::max()};
    for (int year : years) {
      tm extreme{};
      extreme.tm_year = year;
      extreme.tm_mday = 1;
      time_t seconds = LIBC_NAMESPACE::mktime(&extreme);
      ASSERT_NE(seconds, time_t(-1));
      EXPECT_EQ(extreme.tm_year, year);
      EXPECT_EQ(LIBC_NAMESPACE::localtime_r(&seconds, &old), &old);
      EXPECT_EQ(old.tm_year, year);
    }
  }
}
} // namespace
