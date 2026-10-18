//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of internal time utility functions.
///
//===----------------------------------------------------------------------===//

#include "src/time/time_utils.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/limits.h" // INT_MIN, INT_MAX
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/time/posix_timezone.h"
#include "src/time/time_constants.h"

namespace LIBC_NAMESPACE_DECL {
namespace time_utils {

int64_t civil_seconds(const tm *tm_out) {
  // Unlike most C Library functions, mktime doesn't just die on bad input.
  // TODO(rtenneti); Handle leap seconds.
  // POSIX §4.16: each day is accounted for by exactly 86400 seconds.

  // Normalize year and month from tm_mon.
  int64_t total_months = tm_out->tm_mon;
  int64_t year =
      tm_out->tm_year + static_cast<int64_t>(time_constants::TIME_YEAR_BASE);
  int64_t month = total_months % 12;
  year += total_months / 12;
  if (month < 0) {
    month += 12;
    year--;
  }
  month += 1; // 1-12 range
  int64_t day = tm_out->tm_mday;

  int64_t total_days = civil_days(year, static_cast<int>(month), 1) + day - 1;
  // Every int-valued tm input fits this int64_t calculation, including fully
  // unnormalized fields. Check the destination separately for narrow time_t.
  int64_t seconds = int64_t(tm_out->tm_sec) + int64_t(tm_out->tm_min) * 60 +
                    int64_t(tm_out->tm_hour) * 3600 + total_days * 86400;
  return seconds;
}

cpp::optional<time_t> mktime_internal(const tm *tm_out) {
  int64_t seconds = civil_seconds(tm_out);
  if (seconds < cpp::numeric_limits<time_t>::min() ||
      seconds > cpp::numeric_limits<time_t>::max())
    return cpp::nullopt;
  return static_cast<time_t>(seconds);
}

// Convert through March-based 400-year Gregorian eras. All time_t values fit
// the day arithmetic; only the final tm_year needs an int-range check.
ErrorOr<void> update_from_seconds(int64_t total_seconds, tm *result) {
  int64_t days = total_seconds / 86400;
  int64_t remaining_seconds = total_seconds % 86400;
  if (remaining_seconds < 0) {
    remaining_seconds += 86400;
    --days;
  }
  CivilDate date = civil_date(days);
  int64_t tm_year = date.year - time_constants::TIME_YEAR_BASE;
  if (tm_year > INT_MAX || tm_year < INT_MIN)
    return cpp::unexpected(TIME_OVERFLOW);

  int yday =
      time_constants::CUMULATIVE_DAYS_BEFORE_MONTH[date.month] + date.day - 1;
  if (is_leap_year(date.year) && date.month > 2)
    ++yday;
  int wday = static_cast<int>((days + 4) % 7);
  if (wday < 0)
    wday += 7;

  result->tm_year = static_cast<int>(tm_year);
  result->tm_mon = date.month - 1;
  result->tm_mday = date.day;
  result->tm_wday = wday;
  result->tm_yday = yday;
  result->tm_hour = static_cast<int>(remaining_seconds / 3600);
  result->tm_min = static_cast<int>(remaining_seconds / 60 % 60);
  result->tm_sec = static_cast<int>(remaining_seconds % 60);
  result->tm_isdst = 0; // UTC; local conversion supplies timezone metadata.
  return {};
}
} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL
