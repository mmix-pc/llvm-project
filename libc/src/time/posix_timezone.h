//===-- POSIX TZ parsing and recurring transitions --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_TIME_POSIX_TIMEZONE_H
#define LLVM_LIBC_SRC_TIME_POSIX_TIMEZONE_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace time_utils {

// March-based Gregorian eras, with floor division for years before year zero.
// The caller supplies a normalized month in [1, 12].
int64_t civil_days(int64_t year, int month, int day);

struct CivilDate {
  int64_t year;
  int month;
  int day;
};
CivilDate civil_date(int64_t days);

struct TransitionRule {
  enum Kind { JULIAN, DAY_OF_YEAR, MONTH_WEEK_DAY } kind = DAY_OF_YEAR;
  int day = 0;
  int month = 0;
  int week = 0;
  int seconds = 2 * 3600;
  int64_t local_seconds(int64_t year) const;
};

struct PosixTimeZone {
  // These views borrow the parser input; the runtime interns names before
  // publishing a snapshot. Offsets are seconds EAST of UTC (unlike TZ syntax).
  cpp::string_view names[2];
  int offsets[2] = {};
  bool has_dst = false;
  TransitionRule start;
  TransitionRule end;

  // utc_year is the Gregorian year containing the supplied Unix timestamp.
  // Years representable by struct tm, plus adjacent years, fit in int64_t.
  bool is_dst(int64_t seconds, int64_t utc_year) const;
};

enum class TZParseResult { SUCCESS, INVALID, UNSUPPORTED, MISSING_RULES };

// Accepts only explicit POSIX TZ syntax. Does not choose a default for empty
// strings, missing transition rules, or geographic/colon-prefixed names.
// On failure, result is unchanged.
TZParseResult parse_posix_timezone(cpp::string_view text,
                                   PosixTimeZone &result);

} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL

#endif
