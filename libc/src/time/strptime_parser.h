//===-- Locale calendar input ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_TIME_STRPTIME_PARSER_H
#define LLVM_LIBC_SRC_TIME_STRPTIME_PARSER_H

#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/wchar/scalar_codec.h"
#include "src/locale/character.h"
#include "src/locale/time_locale.h"
#include "src/time/time_constants.h"
#include "src/time/time_zone.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Fields are accumulated without reading the caller's possibly uninitialized
// tm. Only conversions that actually occurred are committed on success.
class StrptimeParser {
  using Data = TimeLocaleData;
  const Data *data;
  const __locale_data *ctype;
  cpp::optional<int> year, month, day, weekday, yearday, hour, minute, second,
      isdst;
  cpp::optional<int64_t> century, short_year;
  cpp::optional<int> hour12, afternoon;
  cpp::optional<Data::Era> era;
  cpp::optional<int64_t> era_year;

  static int compare_dates(Data::Date a, Data::Date b) {
    if (a.infinity || b.infinity)
      return (a.infinity > b.infinity) - (a.infinity < b.infinity);
    if (a.year != b.year)
      return a.year < b.year ? -1 : 1;
    if (a.month != b.month)
      return a.month < b.month ? -1 : 1;
    return (a.day > b.day) - (a.day < b.day);
  }

  bool joined_era() {
    if (!era_year || !era)
      return true;
    int64_t delta, full;
    bool backwards = compare_dates(era->start, era->end) > 0;
    bool decreasing = (era->direction < 0) != backwards;
    int64_t start = era->start.year + (era->start.year < 0);
    if (__builtin_sub_overflow(*era_year, era->offset, &delta) ||
        (decreasing && __builtin_sub_overflow(int64_t(0), delta, &delta)) ||
        __builtin_add_overflow(start, delta, &full))
      return false;
    return set_year(full);
  }

  bool parse_era(const char *&input, size_t &available, char conversion,
                 unsigned depth) {
    if (conversion == 'y') {
      int64_t value;
      if (!number(input, available, available,
                  cpp::numeric_limits<int64_t>::min(),
                  cpp::numeric_limits<int64_t>::max(), value, true, false))
        return false;
      era_year = value;
      return joined_era();
    }
    if (conversion == 'C') {
      size_t best = 0;
      cpp::optional<Data::Era> selected;
      for (size_t i = 0; i != data->count(Data::ERAS); ++i) {
        auto candidate = data->era(i);
        if (!candidate)
          return false;
        size_t length = match(input, available, candidate->name);
        if (length > best) {
          best = length;
          selected = *candidate;
        }
      }
      if (!selected)
        return false;
      era = selected;
      input += best;
      available -= best;
      return joined_era();
    }
    // Each alternative is isolated so a failed era format cannot leave fields
    // behind for the next candidate. The category graph bounds recursion.
    for (size_t i = 0; i != data->count(Data::ERAS); ++i) {
      auto candidate = data->era(i);
      if (!candidate)
        return false;
      StrptimeParser trial = *this;
      trial.era = *candidate;
      const char *cursor = input;
      size_t remaining = available;
      if (trial.parse(cursor, candidate->format.data(), remaining, depth + 1) &&
          trial.joined_era()) {
        *this = trial;
        input = cursor;
        available = remaining;
        return true;
      }
    }
    return false;
  }

  cpp::string_view text(Data::Field field, size_t index = 0) const {
    auto item = data->text(field, index);
    if (!item)
      return {};
    return {reinterpret_cast<const char *>(item->bytes.data()),
            item->bytes.size() - 1};
  }

  bool space(unsigned char c) const {
    return classify_byte(c, WCTYPE_SPACE, ctype);
  }

  static size_t scalar_length(const char *input, size_t available) {
    size_t count = 0;
    while (count != available && count != 4 && input[count])
      ++count;
    return count;
  }

  size_t match(const char *input, size_t available,
               cpp::string_view expected) const {
    size_t consumed = 0;
    while (!expected.empty()) {
      auto in = decode_scalar(
          ScalarEncoding::UTF8,
          {reinterpret_cast<const uint8_t *>(input + consumed),
           scalar_length(input + consumed, available - consumed)});
      auto name =
          decode_scalar(ScalarEncoding::UTF8,
                        {reinterpret_cast<const uint8_t *>(expected.data()),
                         expected.size()});
      if (!in || !name ||
          map_wide(in->value, false, ctype) !=
              map_wide(name->value, false, ctype))
        return 0;
      consumed += in->bytes;
      expected.remove_prefix(name->bytes);
    }
    return consumed;
  }

  bool name(const char *&input, size_t &available, Data::Field first,
            unsigned fields, unsigned count, int &value) const {
    size_t best = 0;
    for (unsigned field = 0; field != fields; ++field) {
      for (unsigned i = 0; i != count; ++i) {
        cpp::string_view candidate;
        if (data)
          candidate = text(Data::Field(first + field), i);
        else if (first == Data::ABDAY)
          candidate = field == 0 ? time_constants::WEEK_DAY_NAMES[i]
                                 : time_constants::WEEK_DAY_FULL_NAMES[i];
        else if (first == Data::ABMON)
          candidate = field % 2 == 0 ? time_constants::MONTH_NAMES[i]
                                     : time_constants::MONTH_FULL_NAMES[i];
        else
          candidate = i == 0 ? "AM" : "PM";
        size_t length = match(input, available, candidate);
        if (length > best) {
          best = length;
          value = i;
        }
      }
    }
    if (!best)
      return false;
    input += best;
    available -= best;
    return true;
  }

  bool number(const char *&input, size_t &available, size_t width, int64_t low,
              int64_t high, int64_t &value, bool signed_value,
              bool alternative) const {
    while (available && space(*input)) {
      ++input;
      --available;
    }
    if (width > available)
      width = available;
    if (alternative && data && data->count(Data::ALTERNATIVE_DIGITS)) {
      size_t best = 0;
      for (size_t i = 0; i != data->count(Data::ALTERNATIVE_DIGITS); ++i) {
        if (int64_t(i) < low || int64_t(i) > high)
          continue;
        // A named alternative number can occupy more bytes than its decimal
        // spelling. Modifier-plus-width combinations have unspecified syntax.
        size_t length =
            match(input, available, text(Data::ALTERNATIVE_DIGITS, i));
        if (length > best) {
          best = length;
          value = i;
        }
      }
      if (best) {
        input += best;
        available -= best;
        return true;
      }
      // Numeric symbols can be absent for an individual value.
    }
    size_t i = 0;
    bool negative = false;
    if (signed_value && width && (*input == '+' || *input == '-')) {
      negative = *input == '-';
      ++i;
    }
    size_t digits = i;
    uint64_t magnitude = 0;
    uint64_t limit = negative ? uint64_t(0) - uint64_t(low) : uint64_t(high);
    for (; i != width && internal::isdigit(input[i]); ++i) {
      unsigned digit = input[i] - '0';
      if (magnitude > limit / 10 ||
          (magnitude == limit / 10 && digit > limit % 10))
        return false;
      magnitude = magnitude * 10 + digit;
    }
    if (i == digits)
      return false;
    value = negative ? -int64_t(magnitude - (magnitude != 0)) -
                           int64_t(magnitude != 0)
                     : int64_t(magnitude);
    if (value < low || value > high)
      return false;
    input += i;
    available -= i;
    return true;
  }

  bool set_year(int64_t full) {
    int64_t relative;
    if (__builtin_sub_overflow(full, int64_t(1900), &relative) ||
        relative < cpp::numeric_limits<int>::min() ||
        relative > cpp::numeric_limits<int>::max())
      return false;
    year = int(relative);
    return true;
  }

  bool joined_year() {
    int64_t full;
    if (century) {
      if (__builtin_mul_overflow(*century, int64_t(100), &full) ||
          __builtin_add_overflow(full, short_year.value_or(0), &full))
        return false;
    } else {
      full = *short_year + (*short_year >= 69 ? 1900 : 2000);
    }
    return set_year(full);
  }

public:
  StrptimeParser() : data(current_time_locale()), ctype(current_ctype()) {}

  bool parse(const char *&input, const char *format, size_t &available,
             unsigned depth = 0) {
    if (depth > 8)
      return false;
    while (*format) {
      if (space(*format)) {
        do {
          ++format;
        } while (*format && space(*format));
        while (available && *input && space(*input)) {
          ++input;
          --available;
        }
        continue;
      }
      if (*format++ != '%') {
        if (!available || !*input || *input != format[-1])
          return false;
        ++input;
        --available;
        continue;
      }
      if (*format == '+' || *format == '0')
        ++format;
      size_t width = 0;
      bool has_width = internal::isdigit(*format);
      while (internal::isdigit(*format)) {
        size_t digit = *format++ - '0';
        if (width > (cpp::numeric_limits<size_t>::max() - digit) / 10)
          return false;
        width = width * 10 + digit;
      }
      char modifier = 0;
      if (*format == 'E' || *format == 'O')
        modifier = *format++;
      char conversion = *format;
      if (!conversion)
        return false;
      ++format;
      if (modifier == 'E' && data && data->count(Data::ERAS) &&
          (conversion == 'C' || conversion == 'y' || conversion == 'Y')) {
        if (!parse_era(input, available, conversion, depth))
          return false;
        continue;
      }
      cpp::string_view subformat;
      switch (conversion) {
      case 'c':
      case 'x':
      case 'X':
      case 'r': {
        if (data) {
          auto field = conversion == 'c'   ? Data::DATE_TIME_FORMAT
                       : conversion == 'x' ? Data::DATE_FORMAT
                       : conversion == 'X' ? Data::TIME_FORMAT
                                           : Data::AM_PM_FORMAT;
          if (modifier == 'E' && conversion != 'r') {
            auto alternative = conversion == 'c'   ? Data::ERA_DATE_TIME_FORMAT
                               : conversion == 'x' ? Data::ERA_DATE_FORMAT
                                                   : Data::ERA_TIME_FORMAT;
            subformat = text(alternative);
          }
          if (subformat.empty())
            subformat = text(field);
        } else {
          subformat = conversion == 'c'   ? "%a %b %e %H:%M:%S %Y"
                      : conversion == 'x' ? "%m/%d/%y"
                      : conversion == 'X' ? "%H:%M:%S"
                                          : "%I:%M:%S %p";
        }
        if (subformat.empty())
          return false;
        break;
      }
      case 'D':
        subformat = "%m/%d/%y";
        break;
      case 'R':
        subformat = "%H:%M";
        break;
      case 'T':
        subformat = "%H:%M:%S";
        break;
      default:
        break;
      }
      if (!subformat.empty()) {
        if (!parse(input, subformat.data(), available, depth + 1))
          return false;
        continue;
      }
      if (conversion == 'F') {
        size_t bounded = has_width && width < available ? width : available;
        size_t before = bounded;
        int64_t full;
        if (!number(input, bounded, bounded,
                    int64_t(cpp::numeric_limits<int>::min()) + 1900,
                    int64_t(cpp::numeric_limits<int>::max()) + 1900, full, true,
                    false) ||
            !set_year(full) || !parse(input, "-%m-%d", bounded, depth + 1))
          return false;
        available -= before - bounded;
        continue;
      }
      if (conversion == 'n' || conversion == 't') {
        while (available && *input && space(*input)) {
          ++input;
          --available;
        }
        continue;
      }
      if (conversion == '%') {
        if (!available || *input != '%')
          return false;
        ++input;
        --available;
        continue;
      }
      int index = 0;
      if (conversion == 'a' || conversion == 'A') {
        if (!name(input, available, Data::ABDAY, 2, 7, index))
          return false;
        weekday = index;
        continue;
      }
      if (conversion == 'b' || conversion == 'B' || conversion == 'h') {
        if (!name(input, available, Data::ABMON, 4, 12, index))
          return false;
        month = index;
        continue;
      }
      if (conversion == 'p') {
        if (!name(input, available, Data::AM_PM, 1, 2, index))
          return false;
        afternoon = index;
        if (hour12)
          hour = *hour12 % 12 + 12 * index;
        continue;
      }
      if (conversion == 'Z') {
        auto names = time_utils::timezone_names();
        if (!names)
          return false;
        size_t best = 0;
        int dst = 0;
        for (int i = 0; i != 2; ++i) {
          size_t length =
              match(input, has_width && width < available ? width : available,
                    (*names)[i]);
          if (length > best) {
            best = length;
            dst = i;
          }
        }
        if (!best)
          return false;
        input += best;
        available -= best;
        isdst = dst;
        continue;
      }
      if (conversion == 'z') {
        if (!available || (*input != '+' && *input != '-'))
          return false;
        ++input;
        --available;
        for (unsigned i = 0; i != 4; ++i)
          if (i >= available || !internal::isdigit(input[i]))
            return false;
        if ((input[0] - '0') * 10 + input[1] - '0' > 23 ||
            (input[2] - '0') * 10 + input[3] - '0' > 59)
          return false;
        input += 4;
        available -= 4;
        continue;
      }
      int64_t low = 0, high = 0;
      size_t digits = 2;
      bool sign = false;
      switch (conversion) {
      case 'C':
        low = -99;
        high = 99;
        sign = true;
        break;
      case 'y':
      case 'g':
        low = -99;
        high = 99;
        sign = true;
        break;
      case 'Y':
        low = int64_t(cpp::numeric_limits<int>::min()) + 1900;
        high = int64_t(cpp::numeric_limits<int>::max()) + 1900;
        digits = 4;
        sign = true;
        break;
      case 'G':
      case 's':
        low = cpp::numeric_limits<int64_t>::min();
        high = cpp::numeric_limits<int64_t>::max();
        digits = cpp::numeric_limits<size_t>::max();
        sign = true;
        break;
      case 'd':
      case 'e':
        low = 1;
        high = 31;
        break;
      case 'H':
        high = 23;
        break;
      case 'I':
      case 'm':
        low = 1;
        high = 12;
        break;
      case 'M':
        high = 59;
        break;
      case 'S':
        high = 60;
        break;
      case 'j':
        low = 1;
        high = 366;
        digits = 3;
        break;
      case 'u':
        low = 1;
        high = 7;
        digits = 1;
        break;
      case 'w':
        high = 6;
        digits = 1;
        break;
      case 'U':
      case 'W':
        high = 53;
        break;
      case 'V':
        low = 1;
        high = 53;
        break;
      default:
        return false;
      }
      // Explicit widths also permit expanded years and centuries.
      if (has_width && conversion == 'C') {
        low = cpp::numeric_limits<int64_t>::min() / 100;
        high = cpp::numeric_limits<int64_t>::max() / 100;
      }
      int64_t value;
      if (!number(input, available, has_width ? width : digits, low, high,
                  value, sign, modifier == 'O'))
        return false;
      switch (conversion) {
      case 'C':
        century = value;
        if (!joined_year())
          return false;
        break;
      case 'y':
        short_year = value;
        if (!joined_year())
          return false;
        break;
      case 'Y':
        if (!set_year(value))
          return false;
        break;
      case 'd':
      case 'e':
        day = int(value);
        break;
      case 'H':
        hour = int(value);
        break;
      case 'I':
        hour12 = int(value);
        hour = int(value) % 12 + afternoon.value_or(0) * 12;
        break;
      case 'm':
        month = int(value) - 1;
        break;
      case 'M':
        minute = int(value);
        break;
      case 'S':
        second = int(value);
        break;
      case 'j':
        yearday = int(value) - 1;
        break;
      case 'u':
        weekday = int(value) % 7;
        break;
      case 'w':
        weekday = int(value);
        break;
      default:
        // POSIX leaves the field effects of epoch/week-based conversions
        // unspecified. Validate and consume them without inferring fields.
        break;
      }
    }
    return true;
  }

  bool finish() {
    if (era_year && !era) {
      auto first = data->era(0);
      if (!first)
        return false;
      era = *first;
    }
    if (!joined_era())
      return false;
    if (era) {
      if (!year && !set_year(era->start.year + (era->start.year < 0)))
        return false;
      if (month && day) {
        int64_t full = int64_t(*year) + 1900;
        Data::Date date{full <= 0 ? full - 1 : full, unsigned(*month + 1),
                        unsigned(*day)};
        int from_start = compare_dates(date, era->start);
        int from_end = compare_dates(date, era->end);
        if (!((from_start >= 0 && from_end <= 0) ||
              (from_start <= 0 && from_end >= 0)))
          return false;
      }
    }
    return true;
  }

  void commit(tm &result) const {
    if (year)
      result.tm_year = *year;
    if (month)
      result.tm_mon = *month;
    if (day)
      result.tm_mday = *day;
    if (weekday)
      result.tm_wday = *weekday;
    if (yearday)
      result.tm_yday = *yearday;
    if (hour)
      result.tm_hour = *hour;
    if (minute)
      result.tm_min = *minute;
    if (second)
      result.tm_sec = *second;
    if (isdst)
      result.tm_isdst = *isdst;
  }
};
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
