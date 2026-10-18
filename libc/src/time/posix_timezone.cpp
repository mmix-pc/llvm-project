//===-- POSIX TZ parsing and recurring transitions ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/posix_timezone.h"

namespace LIBC_NAMESPACE_DECL {
namespace time_utils {
namespace {
bool leap(int64_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool alpha(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}
bool digit(char ch) { return ch >= '0' && ch <= '9'; }

class TZParser {
  cpp::string_view remaining;

public:
  explicit TZParser(cpp::string_view input) : remaining(input) {}
  char peek() const { return remaining.empty() ? '\0' : remaining.front(); }
  bool empty() const { return remaining.empty(); }
  bool consume(char ch) {
    if (remaining.empty() || peek() != ch)
      return false;
    remaining.remove_prefix(1);
    return true;
  }
  bool number(int maximum, int &value) {
    if (!digit(peek()))
      return false;
    value = 0;
    do {
      int next = peek() - '0';
      if (value > (maximum - next) / 10 || next > maximum)
        return false;
      value = value * 10 + next;
      remaining.remove_prefix(1);
    } while (digit(peek()));
    return value <= maximum;
  }
  bool name(cpp::string_view &value) {
    bool quoted = consume('<');
    auto begin = remaining;
    while (alpha(peek()) ||
           (quoted && (digit(peek()) || peek() == '+' || peek() == '-')))
      remaining.remove_prefix(1);
    value = begin.substr(0, begin.size() - remaining.size());
    return value.size() >= 3 && (!quoted || consume('>'));
  }
  bool clock(int max_hour, int &seconds) {
    bool negative = consume('-');
    if (!negative)
      consume('+');
    int hour, minute = 0, second = 0;
    if (!number(max_hour, hour))
      return false;
    if (consume(':')) {
      if (!number(59, minute))
        return false;
      if (consume(':') && !number(59, second))
        return false;
    }
    seconds = (hour * 3600 + minute * 60 + second) * (negative ? -1 : 1);
    return true;
  }
  bool rule(TransitionRule &value) {
    if (consume('M')) {
      value.kind = TransitionRule::MONTH_WEEK_DAY;
      if (!number(12, value.month) || value.month == 0 || !consume('.') ||
          !number(5, value.week) || value.week == 0 || !consume('.') ||
          !number(6, value.day))
        return false;
    } else {
      bool julian = consume('J');
      value.kind =
          julian ? TransitionRule::JULIAN : TransitionRule::DAY_OF_YEAR;
      if (!number(365, value.day) || (julian && value.day == 0))
        return false;
    }
    return !consume('/') || clock(167, value.seconds);
  }
};
} // namespace

int64_t civil_days(int64_t year, int month, int day) {
  year -= month <= 2;
  int64_t era = (year >= 0 ? year : year - 399) / 400;
  int64_t year_of_era = year - era * 400;
  int march_month = month + (month > 2 ? -3 : 9);
  int64_t day_of_year = (153 * march_month + 2) / 5 + int64_t(day) - 1;
  return era * 146097 + year_of_era * 365 + year_of_era / 4 -
         year_of_era / 100 + day_of_year - 719468;
}

CivilDate civil_date(int64_t days) {
  days += 719468;
  int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  int64_t day_of_era = days - era * 146097;
  int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
                         day_of_era / 146096) /
                        365;
  int64_t year = era * 400 + year_of_era;
  int day_of_year = static_cast<int>(
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100));
  int march_month = (5 * day_of_year + 2) / 153;
  int day = day_of_year - (153 * march_month + 2) / 5 + 1;
  int month = march_month + (march_month < 10 ? 3 : -9);
  return {year + (month <= 2), month, day};
}

int64_t TransitionRule::local_seconds(int64_t year) const {
  int64_t days = civil_days(year, 1, 1);
  if (kind == JULIAN) {
    days += day - 1 + (leap(year) && day >= 60);
  } else if (kind == DAY_OF_YEAR) {
    days += day;
  } else {
    int64_t first = civil_days(year, month, 1);
    int weekday = static_cast<int>((first + 4) % 7);
    if (weekday < 0)
      weekday += 7;
    int month_day = 1 + (day - weekday + 7) % 7 + (week - 1) * 7;
    int64_t next = month == 12 ? civil_days(year + 1, 1, 1)
                               : civil_days(year, month + 1, 1);
    if (first + month_day - 1 >= next)
      month_day -= 7;
    days = first + month_day - 1;
  }
  return days * 86400 + seconds;
}

bool PosixTimeZone::is_dst(int64_t seconds, int64_t utc_year) const {
  if (!has_dst)
    return false;
  // Select the most recent transition. Nearby years cover transitions
  // displaced across New Year by signed times or UTC offsets. On coincident
  // events, a start wins, including the POSIX all-year-DST specification.
  bool found = false;
  bool daylight_time = false;
  int64_t latest = 0;
  // Two prior years are needed when both prior-year events spill into January.
  for (int delta = -2; delta <= 1; ++delta) {
    int64_t year = utc_year + delta;
    int64_t finish = end.local_seconds(year) - offsets[1];
    if (finish <= seconds && (!found || finish > latest)) {
      found = true;
      latest = finish;
      daylight_time = false;
    }
    int64_t begin = start.local_seconds(year) - offsets[0];
    if (begin <= seconds && (!found || begin >= latest)) {
      found = true;
      latest = begin;
      daylight_time = true;
    }
  }
  return daylight_time;
}

TZParseResult parse_posix_timezone(cpp::string_view text,
                                   PosixTimeZone &result) {
  if (text.starts_with(':'))
    return TZParseResult::UNSUPPORTED;
  TZParser parser(text);
  PosixTimeZone parsed;
  if (!parser.name(parsed.names[0]))
    return TZParseResult::INVALID;
  if (parser.empty() || parser.peek() == '/')
    return TZParseResult::UNSUPPORTED;
  int west;
  if (!parser.clock(24, west))
    return TZParseResult::INVALID;
  parsed.offsets[0] = -west;
  parsed.offsets[1] = parsed.offsets[0];
  if (!parser.empty()) {
    if (!parser.name(parsed.names[1]))
      return TZParseResult::INVALID;
    parsed.has_dst = true;
    parsed.offsets[1] = parsed.offsets[0] + 3600;
    if (parser.peek() != ',' && !parser.empty()) {
      if (!parser.clock(24, west))
        return TZParseResult::INVALID;
      parsed.offsets[1] = -west;
    }
    if (parser.empty())
      return TZParseResult::MISSING_RULES;
    if (!parser.consume(',') || !parser.rule(parsed.start) ||
        !parser.consume(',') || !parser.rule(parsed.end) || !parser.empty())
      return TZParseResult::INVALID;
  }
  result = parsed;
  return TZParseResult::SUCCESS;
}

} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL
