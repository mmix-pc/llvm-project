//===-- Time category text table tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/langinfo_macros.h"
#include "src/locale/time_data.h"
#include "test/UnitTest/Test.h"
#ifdef LIBC_FULL_BUILD
#include "src/__support/libc_errno.h"
#include "src/langinfo/nl_langinfo.h"
#include "src/langinfo/nl_langinfo_l.h"
#include "src/locale/time_category.h"
#include "src/locale/uselocale.h"
#include "src/time/asctime_r.h"
#include "src/time/strftime.h"
#include "src/time/strftime_l.h"
#include "src/time/strptime.h"
#include "src/wchar/wcsftime.h"
#endif

using LIBC_NAMESPACE::internal::LocaleDataView;
using LIBC_NAMESPACE::internal::TimeLocaleData;

namespace {
struct Fixture {
  static constexpr unsigned COUNTS[] = {7, 7, 12, 12, 12, 12, 2, 1,
                                        1, 1, 1,  0,  1,  1,  1, 0};
  uint8_t storage[4096]{};
  uint8_t *bytes = storage + 1;
  size_t size = 0;
  size_t count_offsets[16]{};
  const char *names = "\xc5\x9e";
  unsigned wrong_kind = 16;
  unsigned omit_field = 16;
  const char *values[16]{};
  unsigned counts[16];
  Fixture() {
    for (unsigned i = 0; i != 16; ++i)
      counts[i] = COUNTS[i];
  }
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - i * 8));
  }
  void build() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    unsigned entries = 0;
    for (unsigned field = 0; field != 16; ++field)
      if (field != omit_field)
        entries += 1 + counts[field];
    size_t table = 130;
    size_t payload = table + entries * 16;
    auto item = [&](uint32_t key, unsigned kind, size_t length) {
      put(bytes + table, key);
      put(bytes + table + 4, kind);
      put(bytes + table + 8, uint32_t(payload - 130));
      put(bytes + table + 12, uint32_t(length));
      table += 16;
    };
    for (unsigned field = 0; field != 16; ++field) {
      if (field == omit_field)
        continue;
      uint32_t key = (field + 1) << 16;
      item(key, field == wrong_kind ? 3 : 2, 4);
      count_offsets[field] = payload;
      put(bytes + payload, counts[field]);
      payload += 4;
      for (unsigned i = 0; i != counts[field]; ++i) {
        const char *text = values[field] ? values[field]
                           : field < 6   ? names
                                         : "";
        size_t length = 1;
        while (text[length - 1])
          ++length;
        item(key + i + 1, 1, length);
        for (size_t j = 0; j != length; ++j)
          bytes[payload + j] = uint8_t(text[j]);
        payload += length;
      }
    }
    size = payload;
    put(bytes + 16, uint32_t(size));
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, i <= 2 ? 130 : uint32_t(size));
      put(directory + 8, i == 2 ? uint32_t(size - 130) : 0);
      put(directory + 12, i == 2 ? entries : 0);
    }
  }
  bool accepted() {
    auto file = LocaleDataView::parse({bytes, size});
    return file && TimeLocaleData::read(*file).has_value();
  }
};
} // namespace

TEST(LlvmLibcTimeDataTest, TablesAndEmptyValues) {
  Fixture f;
  f.build();
  auto file = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(file.has_value());
  auto time = TimeLocaleData::read(*file);
  ASSERT_TRUE(time.has_value());
  EXPECT_EQ(time->count(TimeLocaleData::DAY), size_t(7));
  EXPECT_EQ(time->count(TimeLocaleData::ALT_MON), size_t(12));
  EXPECT_EQ(time->count(TimeLocaleData::ERAS), size_t(0));
  auto name = time->text(TimeLocaleData::MON, 11);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(name->bytes.size(), size_t(3));
  EXPECT_EQ(name->bytes[0], uint8_t(0xc5));
  auto am = time->text(TimeLocaleData::AM_PM, 1);
  ASSERT_TRUE(am.has_value());
  EXPECT_EQ(am->bytes.size(), size_t(1));
  EXPECT_FALSE(time->text(TimeLocaleData::MON, 12).has_value());
  EXPECT_FALSE(time->text(TimeLocaleData::ERAS, 0).has_value());
  EXPECT_FALSE(time->text(TimeLocaleData::FIELD_COUNT, 0).has_value());
  EXPECT_EQ(time->count(TimeLocaleData::FIELD_COUNT), size_t(0));
}

#ifdef LIBC_FULL_BUILD
TEST(LlvmLibcTimeDataTest, CalendarParsingUsesSelectedCategory) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.values[TimeLocaleData::DATE_TIME_FORMAT] = "%x %X";
  f.values[TimeLocaleData::DATE_FORMAT] = "%Y/%m/%d";
  f.values[TimeLocaleData::TIME_FORMAT] = "%H:%M";
  f.values[TimeLocaleData::ALT_MON] = "alternative";
  f.values[TimeLocaleData::ABALT_MON] = "alt";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = TimeLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  __locale_data category{"time-test", true};
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*data);
  auto selected = locale_from_mask(0);
  selected.__data[LC_TIME] = &category;
  auto old = LIBC_NAMESPACE::uselocale(&selected);
  tm time{};
  time.tm_yday = 71;
  time.tm_isdst = -1;
  char input[] = "2026/10/25 16:05!";
  EXPECT_EQ(LIBC_NAMESPACE::strptime(input, "%c", &time), input + 16);
  EXPECT_EQ(time.tm_year, 126);
  EXPECT_EQ(time.tm_mon, 9);
  EXPECT_EQ(time.tm_mday, 25);
  EXPECT_EQ(time.tm_hour, 16);
  EXPECT_EQ(time.tm_min, 5);
  EXPECT_EQ(time.tm_yday, 71);
  EXPECT_EQ(time.tm_isdst, -1);
  char name[] = "ALTERNATIVE!";
  EXPECT_EQ(LIBC_NAMESPACE::strptime(name, "%Ob", &time), name + 11);
  EXPECT_EQ(time.tm_mon, 0);
  EXPECT_NE(LIBC_NAMESPACE::strptime("\xc5\x9e", "%A", &time), nullptr);
  EXPECT_EQ(time.tm_wday, 0);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("\xc5", "%A", &time), nullptr);
  LIBC_NAMESPACE::uselocale(old);
}

TEST(LlvmLibcTimeDataTest, EraAndAlternativeNumberParsing) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.counts[TimeLocaleData::ERAS] = 1;
  f.values[TimeLocaleData::ERAS] = "+:1:2019/5/1:+*:era:%EC-%Ey";
  f.values[TimeLocaleData::ERA_DATE_FORMAT] = "%EY/%m/%d";
  f.counts[TimeLocaleData::ALTERNATIVE_DIGITS] = 2;
  f.values[TimeLocaleData::ALTERNATIVE_DIGITS] = "alternative";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = TimeLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  __locale_data category{"era-test", true};
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*data);
  auto selected = locale_from_mask(0);
  selected.__data[LC_TIME] = &category;
  auto old = LIBC_NAMESPACE::uselocale(&selected);
  tm time{};
  char input[] = "ERA-08/10/25 alternative!";
  EXPECT_EQ(LIBC_NAMESPACE::strptime(input, "%Ex %OS", &time), input + 24);
  EXPECT_EQ(time.tm_year, 126);
  EXPECT_EQ(time.tm_mon, 9);
  EXPECT_EQ(time.tm_mday, 25);
  EXPECT_EQ(time.tm_sec, 0);
  EXPECT_NE(LIBC_NAMESPACE::strptime("03 era", "%Ey %EC", &time), nullptr);
  EXPECT_EQ(time.tm_year, 121);
  EXPECT_NE(LIBC_NAMESPACE::strptime("03", "%Ey", &time), nullptr);
  EXPECT_EQ(time.tm_year, 121);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("era-01/04/30", "%Ex", &time), nullptr);
  EXPECT_EQ(time.tm_year, 121);
  EXPECT_NE(LIBC_NAMESPACE::strptime("era", "%EC", &time), nullptr);
  EXPECT_EQ(time.tm_year, 119);
  EXPECT_EQ(LIBC_NAMESPACE::strptime("era-9223372036854775807", "%EY", &time),
            nullptr);
  EXPECT_EQ(time.tm_year, 119);
  LIBC_NAMESPACE::uselocale(old);
}

TEST(LlvmLibcTimeDataTest, BackwardEraParsing) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.counts[TimeLocaleData::ERAS] = 1;
  f.values[TimeLocaleData::ERAS] = "+:1:-1/12/31:-*:BC:%Ey %EC";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = TimeLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  __locale_data category{"era-test", true};
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*data);
  auto selected = locale_from_mask(0);
  selected.__data[LC_TIME] = &category;
  auto old = LIBC_NAMESPACE::uselocale(&selected);
  tm time{};
  EXPECT_NE(LIBC_NAMESPACE::strptime("03 BC", "%EY", &time), nullptr);
  EXPECT_EQ(time.tm_year, -1902);
  EXPECT_NE(LIBC_NAMESPACE::strptime("01 bc", "%EY", &time), nullptr);
  EXPECT_EQ(time.tm_year, -1900);
  LIBC_NAMESPACE::uselocale(old);
}

TEST(LlvmLibcTimeDataTest, CalendarFormattingUsesSelectedCategory) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.values[TimeLocaleData::DATE_TIME_FORMAT] = "%x %X";
  f.values[TimeLocaleData::DATE_FORMAT] = "%Y/%m/%d";
  f.values[TimeLocaleData::TIME_FORMAT] = "%H:%M";
  f.values[TimeLocaleData::AM_PM] = "period";
  f.values[TimeLocaleData::AM_PM_FORMAT] = "%I %p";
  f.values[TimeLocaleData::ALT_MON] = "alternative";
  f.values[TimeLocaleData::ABALT_MON] = "alt";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto time_data = TimeLocaleData::read(*view);
  ASSERT_TRUE(time_data.has_value());
  __locale_data category{"time-test", true};
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*time_data);
  auto selected = locale_from_mask(0);
  selected.__data[LC_TIME] = &category;
  auto old = LIBC_NAMESPACE::uselocale(&selected);
  tm time{};
  time.tm_year = 126;
  time.tm_mon = 9;
  time.tm_mday = 25;
  time.tm_hour = 16;
  time.tm_min = 5;
  char out[128];
  EXPECT_GT(
      LIBC_NAMESPACE::strftime(out, sizeof(out), "%a %B %c %r %Ob %OB", &time),
      size_t(0));
  EXPECT_STREQ(out,
               "\xc5\x9e \xc5\x9e 2026/10/25 16:05 04 period alt alternative");
  auto c = locale_from_mask(0);
  EXPECT_GT(LIBC_NAMESPACE::strftime_l(out, sizeof(out), "%a %B %x", &time, &c),
            size_t(0));
  EXPECT_STREQ(out, "Sun October 10/25/26");
  EXPECT_TRUE(LIBC_NAMESPACE::uselocale(nullptr) == &selected);
  EXPECT_NE(LIBC_NAMESPACE::asctime_r(&time, out), nullptr);
  EXPECT_STREQ(out, "Sun Oct 25 16:05:00 2026\n");
  wchar_t wide[32];
  EXPECT_EQ(LIBC_NAMESPACE::wcsftime(wide, 32, L"%B %p", &time), size_t(8));
  const wchar_t expected[] = L"\u015e period";
  for (size_t i = 0; i != 9; ++i)
    EXPECT_EQ(wide[i], expected[i]);
  LIBC_NAMESPACE::uselocale(old);
}

TEST(LlvmLibcTimeDataTest, EraAndAlternativeNumberFormatting) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.counts[TimeLocaleData::ERAS] = 1;
  f.values[TimeLocaleData::ERAS] = "+:1:2019/5/1:+*:era:%EC-%Ey";
  f.values[TimeLocaleData::ERA_DATE_FORMAT] = "%EY/%m/%d";
  f.counts[TimeLocaleData::ALTERNATIVE_DIGITS] = 2;
  f.values[TimeLocaleData::ALTERNATIVE_DIGITS] = "alternative";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = TimeLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  __locale_data category{"era-test", true};
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*data);
  auto selected = locale_from_mask(0);
  selected.__data[LC_TIME] = &category;
  tm time{};
  time.tm_year = 126;
  time.tm_mon = 9;
  time.tm_mday = 25;
  char out[128];
  EXPECT_GT(LIBC_NAMESPACE::strftime_l(
                out, sizeof(out), "%EC|%Ey|%EY|%Ex|%OS|%Od", &time, &selected),
            size_t(0));
  EXPECT_STREQ(out, "era|08|era-08|era-08/10/25|alternative|25");
  time.tm_year = 119;
  time.tm_mon = 3;
  time.tm_mday = 30;
  EXPECT_GT(LIBC_NAMESPACE::strftime_l(out, sizeof(out), "%EC|%Ey|%EY", &time,
                                       &selected),
            size_t(0));
  EXPECT_STREQ(out, "20|19|2019");
}

TEST(LlvmLibcTimeDataTest, BackwardEraAndOverflow) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.counts[TimeLocaleData::ERAS] = 1;
  f.values[TimeLocaleData::ERAS] = "+:1:-1/12/31:-*:BC:%Ey %EC";
  f.build();
  auto view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  auto data = TimeLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  __locale_data category{"era-test", true};
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*data);
  auto selected = locale_from_mask(0);
  selected.__data[LC_TIME] = &category;
  tm time{};
  time.tm_year = -1902;
  time.tm_mday = 1;
  char out[32];
  EXPECT_GT(
      LIBC_NAMESPACE::strftime_l(out, sizeof(out), "%EY", &time, &selected),
      size_t(0));
  EXPECT_STREQ(out, "03 BC");
  f.values[TimeLocaleData::ERAS] = "+:9223372036854775807:2000/1/1:+*:era:%Ey";
  f.build();
  view = LocaleDataView::parse({f.bytes, f.size});
  ASSERT_TRUE(view.has_value());
  data = TimeLocaleData::read(*view);
  ASSERT_TRUE(data.has_value());
  category.time = LIBC_NAMESPACE::cpp::optional<TimeLocaleData>(*data);
  time.tm_year = 126;
  EXPECT_EQ(
      LIBC_NAMESPACE::strftime_l(out, sizeof(out), "%Ey", &time, &selected),
      size_t(0));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EOVERFLOW);
}

TEST(LlvmLibcTimeDataTest, RegionalQueriesRetainBackingStorage) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  const char *strings[] = {
      "abday",  "day",   "abmon", "mon", "altmon", "abaltmon",
      "period", "%a %Y", "%Y",    "%H",  "%I %p",  "+:1:2000/1/1:+*:era:%Y",
      "%Y-%m",  "%H:%M", "%Y %H", "zero"};
  for (unsigned i = 0; i != 16; ++i)
    f.values[i] = strings[i];
  f.counts[TimeLocaleData::ERAS] = 2;
  f.counts[TimeLocaleData::ALTERNATIVE_DIGITS] = 2;
  f.build();
  auto file = LocaleDataFile::copy({f.bytes, f.size});
  ASSERT_TRUE(file.has_value());
  auto category = create_time_category(**file);
  (*file)->release();
  ASSERT_TRUE(category.has_value());
  auto locale = locale_from_mask(0);
  locale.__data[LC_TIME] = *category;
  auto previous = LIBC_NAMESPACE::uselocale(&locale);
  struct Range {
    nl_item first;
    unsigned count;
    unsigned field;
  };
  const Range ranges[] = {
      {ABDAY_1, 7, 0},   {DAY_1, 7, 1},       {ABMON_1, 12, 2}, {MON_1, 12, 3},
      {ALTMON_1, 12, 4}, {ABALTMON_1, 12, 5}, {AM_STR, 2, 6}};
  for (auto range : ranges)
    for (unsigned i = 0; i != range.count; ++i) {
      EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(range.first + i),
                   strings[range.field]);
      EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(range.first + i, &locale),
                   strings[range.field]);
    }
  const nl_item formats[] = {D_T_FMT,   D_FMT,     T_FMT,      T_FMT_AMPM,
                             ERA_D_FMT, ERA_T_FMT, ERA_D_T_FMT};
  for (unsigned i = 0; i != 7; ++i)
    EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(formats[i]),
                 strings[i < 4 ? i + 7 : i + 8]);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(ERA),
               "+:1:2000/1/1:+*:era:%Y;+:1:2000/1/1:+*:era:%Y");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(ALT_DIGITS), "zero;zero");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(-1), "");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(RADIXCHAR), ".");
  LIBC_NAMESPACE::uselocale(previous);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(DAY_1, &locale), "day");
  release_locale(locale);
}

TEST(LlvmLibcTimeDataTest, EmptyEraAndAlternativeDigitQueries) {
  using namespace LIBC_NAMESPACE::internal;
  Fixture f;
  f.build();
  auto file = LocaleDataFile::copy({f.bytes, f.size});
  ASSERT_TRUE(file.has_value());
  auto category = create_time_category(**file);
  (*file)->release();
  ASSERT_TRUE(category.has_value());
  auto locale = locale_from_mask(0);
  locale.__data[LC_TIME] = *category;
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(ERA, &locale), "");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(ALT_DIGITS, &locale), "");
  release_locale(locale);
}
#endif

TEST(LlvmLibcTimeDataTest, RejectMissingOrMistypedCount) {
  for (unsigned field = 0; field != 16; ++field) {
    Fixture f;
    f.omit_field = field;
    f.build();
    EXPECT_FALSE(f.accepted());
    Fixture wrong;
    wrong.wrong_kind = field;
    wrong.build();
    EXPECT_FALSE(wrong.accepted());
  }
}

TEST(LlvmLibcTimeDataTest, RejectInvalidCardinality) {
  for (unsigned field = 0; field != 16; ++field) {
    Fixture f;
    f.build();
    Fixture::put(f.bytes + f.count_offsets[field], uint32_t(-1));
    EXPECT_FALSE(f.accepted());
    Fixture::put(f.bytes + f.count_offsets[field], Fixture::COUNTS[field] + 1);
    EXPECT_FALSE(f.accepted());
  }
}

TEST(LlvmLibcTimeDataTest, RejectInvalidOrEmptyNames) {
  const char *bad[] = {"", "\xc0\x80", "\xed\xa0\x80", "\xe2\x82"};
  for (const char *name : bad) {
    Fixture f;
    f.names = name;
    f.build();
    EXPECT_FALSE(f.accepted());
  }
}

TEST(LlvmLibcTimeDataTest, EraDatesAndBorrowedText) {
  auto forward = TimeLocaleData::parse_era("+:1:2019/05/01:+*:Reiwa:%EC%Ey");
  ASSERT_TRUE(forward.has_value());
  EXPECT_EQ(forward->direction, 1);
  EXPECT_EQ(forward->offset, int64_t(1));
  EXPECT_EQ(forward->start.year, int64_t(2019));
  EXPECT_EQ(forward->start.month, 5U);
  EXPECT_EQ(forward->end.infinity, 1);
  EXPECT_TRUE(forward->name == "Reiwa");
  EXPECT_TRUE(forward->format == "%EC%Ey");
  auto backward = TimeLocaleData::parse_era("+:1:-1/12/31:-*:BC:%Ey %EC");
  ASSERT_TRUE(backward.has_value());
  EXPECT_EQ(backward->start.year, int64_t(-1));
  EXPECT_EQ(backward->end.infinity, -1);
  EXPECT_TRUE(TimeLocaleData::parse_era("-:7:2000/02/29:1900/02/28:x:%Y:%M")
                  .has_value());
  EXPECT_TRUE(TimeLocaleData::parse_era("+:1:-1/02/29:-*:BC:%Ey").has_value());
  EXPECT_TRUE(TimeLocaleData::parse_era("+:-9223372036854775808:1/1/1:+*:x:%Ey")
                  .has_value());
}

TEST(LlvmLibcTimeDataTest, RejectMalformedEraDates) {
  const char *bad[] = {
      "+:1:1900/02/29:+*:x:%Ey",
      "+:1:2023/04/31:+*:x:%Ey",
      "+:1:0/1/1:+*:x:%Ey",
      "+:1:2024/13/1:+*:x:%Ey",
      "+:1:2024/1/0:+*:x:%Ey",
      "+:1:+*:+*:x:%Ey",
      "?:1:2024/1/1:+*:x:%Ey",
      "+:x:2024/1/1:+*:x:%Ey",
      "+:9223372036854775808:1/1/1:+*:x:%Ey",
      "+:-9223372036854775809:1/1/1:+*:x:%Ey",
      "+:1:9223372036854775808/1/1:+*:x:%Ey",
      "+:1:2024/1/1:2024/2/30:x:%Ey",
      "+:1:2024/1/1:+*:x",
  };
  for (const char *value : bad)
    EXPECT_FALSE(TimeLocaleData::parse_era(value).has_value());
}

TEST(LlvmLibcTimeDataTest, FormatsAndModifiers) {
  Fixture f;
  f.values[TimeLocaleData::DATE_TIME_FORMAT] = "%a %d %b %+12Y %X %% %Ec";
  f.values[TimeLocaleData::TIME_FORMAT] = "%OH:%OM:%OS";
  f.values[TimeLocaleData::ERA_DATE_TIME_FORMAT] = "%EY %OB %Od";
  f.values[TimeLocaleData::ERAS] = "+:1:2019/05/01:+*:Reiwa:%EC%Ey";
  f.counts[TimeLocaleData::ERAS] = 1;
  f.build();
  EXPECT_TRUE(f.accepted());
  const char *bad[] = {"%",
                       "%E",
                       "%O",
                       "%Q",
                       "%OE",
                       "%Ea",
                       "%Or",
                       "%+2147483648Y",
                       "%+999999999999999999999Y"};
  for (const char *format : bad) {
    f.values[TimeLocaleData::DATE_FORMAT] = format;
    f.build();
    EXPECT_FALSE(f.accepted());
  }
}

TEST(LlvmLibcTimeDataTest, RejectFormatCycles) {
  const char *cycles[] = {"%c", "%Ec"};
  for (const char *format : cycles) {
    Fixture f;
    f.values[TimeLocaleData::DATE_TIME_FORMAT] = format;
    f.build();
    EXPECT_FALSE(f.accepted());
  }
  Fixture f;
  f.values[TimeLocaleData::DATE_FORMAT] = "%X";
  f.values[TimeLocaleData::TIME_FORMAT] = "%x";
  f.build();
  EXPECT_FALSE(f.accepted());
  f.values[TimeLocaleData::TIME_FORMAT] = "%H:%M:%S";
  f.build();
  EXPECT_TRUE(f.accepted());
  f.values[TimeLocaleData::ERAS] = "+:1:2000/1/1:+*:x:%EY";
  f.counts[TimeLocaleData::ERAS] = 1;
  f.build();
  EXPECT_FALSE(f.accepted());
  f.values[TimeLocaleData::ERAS] = "+:1:2000/1/1:+*:x:%Ex";
  f.values[TimeLocaleData::DATE_FORMAT] = "%EY";
  f.build();
  EXPECT_FALSE(f.accepted());
}

TEST(LlvmLibcTimeDataTest, TwelveHourRepresentation) {
  Fixture f;
  f.values[TimeLocaleData::AM_PM_FORMAT] = "%I:%M:%S %p";
  f.build();
  EXPECT_FALSE(f.accepted());
  f.values[TimeLocaleData::AM_PM] = "AM";
  f.build();
  EXPECT_TRUE(f.accepted());
  f.values[TimeLocaleData::AM_PM_FORMAT] = "";
  f.build();
  EXPECT_FALSE(f.accepted());
}
