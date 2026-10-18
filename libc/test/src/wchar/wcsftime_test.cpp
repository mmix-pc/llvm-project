//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/strftime.h"
#include "src/wchar/wcsftime.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

static tm date() {
  tm t{};
  t.tm_year = 124;
  t.tm_mon = 1;
  t.tm_mday = 29;
  t.tm_hour = 13;
  t.tm_min = 5;
  t.tm_sec = 7;
  t.tm_wday = 4;
  t.tm_yday = 59;
  t.tm_isdst = 0;
  return t;
}

TEST(LlvmLibcWcsftimeTest, LiteralsAndComposite) {
  LocaleGuard locale;
  tm t = date();
  wchar_t buffer[80];
  constexpr wchar_t expected[] = L"\u65e5\u671f 2024-02-29 13:05:07 \U0001f600";
  size_t n = LIBC_NAMESPACE::wcsftime(buffer, 80,
                                      L"\u65e5\u671f %F %T \U0001f600", &t);
  ASSERT_EQ(n, sizeof(expected) / sizeof(wchar_t) - 1);
  for (size_t i = 0; i <= n; ++i)
    EXPECT_EQ(buffer[i], expected[i]);
}

TEST(LlvmLibcWcsftimeTest, StandardConversionsMatchSharedEngine) {
  LocaleGuard locale;
  tm t = date();
  constexpr char specs[] = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
  for (char spec : specs) {
    if (!spec)
      break;
    char format[] = {'%', spec, 0};
    wchar_t wide_format[] = {L'%', wchar_t(spec), 0};
    char expected[128];
    wchar_t actual[128];
    size_t n = LIBC_NAMESPACE::strftime(expected, 128, format, &t);
    ASSERT_EQ(LIBC_NAMESPACE::wcsftime(actual, 128, wide_format, &t), n);
    for (size_t i = 0; i <= n; ++i)
      EXPECT_EQ(actual[i], wchar_t(expected[i]));
  }
}

TEST(LlvmLibcWcsftimeTest, LocaleModifiers) {
  LocaleGuard locale;
  tm t = date();
  for (char modifier : "EO") {
    if (!modifier)
      break;
    const char *specs = modifier == 'E' ? "cCxXyY" : "deHImMSuUVwWy";
    for (; *specs; ++specs) {
      char spec = *specs;
      char format[] = {'%', modifier, spec, 0};
      wchar_t wide_format[] = {L'%', wchar_t(modifier), wchar_t(spec), 0};
      char expected[128];
      wchar_t actual[128];
      size_t n = LIBC_NAMESPACE::strftime(expected, 128, format, &t);
      ASSERT_EQ(LIBC_NAMESPACE::wcsftime(actual, 128, wide_format, &t), n);
      for (size_t i = 0; i <= n; ++i)
        EXPECT_EQ(actual[i], wchar_t(expected[i]));
    }
  }
}

TEST(LlvmLibcWcsftimeTest, BufferBounds) {
  LocaleGuard locale;
  tm t = date();
  constexpr wchar_t format[] = L"\u20ac%Y";
  wchar_t output[8];
  for (size_t capacity = 0; capacity <= 6; ++capacity) {
    for (auto &c : output)
      c = L'!';
    size_t n = LIBC_NAMESPACE::wcsftime(output + 1, capacity, format, &t);
    EXPECT_EQ(n, capacity == 6 ? size_t(5) : size_t(0));
    EXPECT_EQ(output[0], L'!');
    EXPECT_EQ(output[capacity + 1], L'!');
    if (n)
      EXPECT_EQ(output[n + 1], L'\0');
  }
  EXPECT_EQ(LIBC_NAMESPACE::wcsftime(nullptr, 0, format, &t), size_t(0));
}

TEST(LlvmLibcWcsftimeTest, EmptyOutputAndUnknownZone) {
  LocaleGuard locale;
  tm t = date();
  wchar_t output[2] = {L'!', L'!'};
  EXPECT_EQ(LIBC_NAMESPACE::wcsftime(output, 1, L"", &t), size_t(0));
  EXPECT_EQ(output[0], L'\0');
  EXPECT_EQ(output[1], L'!');
  t.tm_isdst = -1;
  EXPECT_EQ(LIBC_NAMESPACE::wcsftime(output, 1, L"%z%Z", &t), size_t(0));
  EXPECT_EQ(output[0], L'\0');
}

TEST(LlvmLibcWcsftimeTest, IsoYearBoundary) {
  LocaleGuard locale;
  tm t{};
  t.tm_year = 121;
  t.tm_mday = 1;
  t.tm_wday = 5;
  wchar_t output[32];
  constexpr wchar_t expected[] = L"2020-W53-5";
  ASSERT_EQ(LIBC_NAMESPACE::wcsftime(output, 32, L"%G-W%V-%u", &t), size_t(10));
  for (size_t i = 0; i < 11; ++i)
    EXPECT_EQ(output[i], expected[i]);
}

#if defined(__linux__)
TEST(LlvmLibcWcsftimeTest, MultibyteZone) {
  LocaleGuard locale;
  tm t = date();
  t.tm_zone = "\xe2\x82\xac";
  wchar_t output[3] = {L'!', L'!', L'!'};
  ASSERT_EQ(LIBC_NAMESPACE::wcsftime(output, 2, L"%Z", &t), size_t(1));
  EXPECT_EQ(output[0], wchar_t(0x20ac));
  EXPECT_EQ(output[1], L'\0');
  EXPECT_EQ(output[2], L'!');
  t.tm_zone = "\xe2\x82";
  EXPECT_EQ(LIBC_NAMESPACE::wcsftime(output, 3, L"%Z", &t), size_t(0));
  t.tm_zone = "\xed\xa0\x80";
  EXPECT_EQ(LIBC_NAMESPACE::wcsftime(output, 3, L"%Z", &t), size_t(0));
}
#endif
