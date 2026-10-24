//===-- Calendar formatting capacity and errno tests
//-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/locale/locale.h"
#include "src/time/strftime.h"
#include "src/time/strftime_l.h"
#include "src/wchar/wcsftime.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

namespace {
size_t format(bool explicit_locale, char *buffer, size_t size,
              const char *pattern, const tm *date) {
  if (!explicit_locale)
    return LIBC_NAMESPACE::strftime(buffer, size, pattern, date);
  auto locale = LIBC_NAMESPACE::internal::locale_from_mask(0);
  return LIBC_NAMESPACE::strftime_l(buffer, size, pattern, date, &locale);
}
} // namespace

TEST(LlvmLibcStrftimeCapacityTest, NarrowCapacityAndSuccessfulErrno) {
  LocaleGuard locale(false);
  tm date{};
  date.tm_year = 124;
  const bool modes[] = {false, true};
  for (bool explicit_locale : modes) {
    for (size_t size = 0; size <= 6; ++size) {
      char bytes[8] = {'!', '?', '?', '?', '?', '?', '?', '!'};
      libc_errno = EDOM;
      size_t count = format(explicit_locale, bytes + 1, size, "%Y", &date);
      int error = libc_errno;
      EXPECT_EQ(count, size <= 4 ? size_t(0) : size_t(4));
      EXPECT_EQ(error, size <= 4 ? ERANGE : EDOM);
      EXPECT_EQ(bytes[0], '!');
      EXPECT_EQ(bytes[7], '!');
      if (size < 6)
        EXPECT_EQ(bytes[size + 1], '?');
      if (size > 4)
        EXPECT_STREQ(bytes + 1, "2024");
    }
    libc_errno = EDOM;
    EXPECT_EQ(format(explicit_locale, nullptr, 0, "%Y", &date), size_t(0));
    EXPECT_EQ(int(libc_errno), ERANGE);
  }
}

TEST(LlvmLibcStrftimeCapacityTest, EmptySuccessIsNotCapacityFailure) {
  LocaleGuard locale(false);
  tm date{};
  date.tm_isdst = -1;
  const bool modes[] = {false, true};
  const char *patterns[] = {"", "%z%Z"};
  for (bool explicit_locale : modes) {
    for (const char *pattern : patterns) {
      char bytes[2] = {'?', '!'};
      libc_errno = EDOM;
      EXPECT_EQ(format(explicit_locale, bytes, 1, pattern, &date), size_t(0));
      EXPECT_EQ(int(libc_errno), EDOM);
      EXPECT_EQ(bytes[0], '\0');
      EXPECT_EQ(bytes[1], '!');
      libc_errno = EDOM;
      EXPECT_EQ(format(explicit_locale, nullptr, 0, pattern, &date), size_t(0));
      EXPECT_EQ(int(libc_errno), ERANGE);
    }
  }
}

TEST(LlvmLibcStrftimeCapacityTest, WideCapacityKeepsSeparateErrorPolicy) {
  LocaleGuard locale(false);
  tm date{};
  date.tm_year = 124;
  for (size_t size = 0; size <= 5; ++size) {
    wchar_t bytes[7] = {L'!', L'?', L'?', L'?', L'?', L'?', L'!'};
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::wcsftime(bytes + 1, size, L"%Y", &date),
              size == 5 ? size_t(4) : size_t(0));
    EXPECT_EQ(int(libc_errno), EDOM);
    EXPECT_EQ(bytes[0], L'!');
    EXPECT_EQ(bytes[6], L'!');
  }
}
