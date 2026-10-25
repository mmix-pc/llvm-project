//===-- Built-in locale information ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/langinfo/langinfo.h"
#include "hdr/langinfo_macros.h"
#include "src/locale/locale.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
char *langinfo(nl_item item, locale_t loc) {
  auto locale = locale_snapshot(loc);
  if ((item == YESEXPR || item == NOEXPR) &&
      locale.data[LC_MESSAGES]->messages) {
    const auto &messages = *locale.data[LC_MESSAGES]->messages;
    auto text = item == YESEXPR ? messages.yesexpr : messages.noexpr;
    return const_cast<char *>(
        reinterpret_cast<const char *>(text.bytes.data()));
  }
  if (item == RADIXCHAR || item == THOUSEP) {
    auto *category = locale.data[LC_NUMERIC];
    if (category->numeric) {
      const auto &field = item == RADIXCHAR ? category->numeric->decimal_point
                                            : category->numeric->thousands_sep;
      return const_cast<char *>(
          reinterpret_cast<const char *>(field.bytes.data()));
    }
  } else if (item == CRNCYSTR && locale.data[LC_MONETARY]->monetary) {
    return locale.data[LC_MONETARY]->currency;
  }
  auto *time = locale.data[LC_TIME];
  if (time->time) {
    using T = TimeLocaleData;
    T::Field field = T::FIELD_COUNT;
    size_t index = 0;
    struct Range {
      nl_item first, last;
      T::Field field;
    };
    constexpr Range ranges[] = {{DAY_1, DAY_7, T::DAY},
                                {ABDAY_1, ABDAY_7, T::ABDAY},
                                {MON_1, MON_12, T::MON},
                                {ABMON_1, ABMON_12, T::ABMON},
                                {ALTMON_1, ALTMON_12, T::ALT_MON},
                                {ABALTMON_1, ABALTMON_12, T::ABALT_MON},
                                {AM_STR, PM_STR, T::AM_PM}};
    for (const auto &range : ranges)
      if (item >= range.first && item <= range.last) {
        field = range.field;
        index = size_t(item - range.first);
        break;
      }
    switch (item) {
    case ERA:
      return time->eras;
    case ALT_DIGITS:
      return time->alternative_digits;
    case D_T_FMT:
      field = T::DATE_TIME_FORMAT;
      break;
    case D_FMT:
      field = T::DATE_FORMAT;
      break;
    case T_FMT:
      field = T::TIME_FORMAT;
      break;
    case T_FMT_AMPM:
      field = T::AM_PM_FORMAT;
      break;
    case ERA_D_FMT:
      field = T::ERA_DATE_FORMAT;
      break;
    case ERA_T_FMT:
      field = T::ERA_TIME_FORMAT;
      break;
    case ERA_D_T_FMT:
      field = T::ERA_DATE_TIME_FORMAT;
      break;
    default:
      break;
    }
    if (field != T::FIELD_COUNT) {
      const auto text = time->time->text(field, index).value();
      return const_cast<char *>(
          reinterpret_cast<const char *>(text.bytes.data()));
    }
  }
  // Both built-ins use the C locale's non-character categories.
  static const char *const days[] = {"Sunday",    "Monday",   "Tuesday",
                                     "Wednesday", "Thursday", "Friday",
                                     "Saturday"};
  static const char *const abdays[] = {"Sun", "Mon", "Tue", "Wed",
                                       "Thu", "Fri", "Sat"};
  static const char *const months[] = {
      "January", "February", "March",     "April",   "May",      "June",
      "July",    "August",   "September", "October", "November", "December"};
  static const char *const abmonths[] = {"Jan", "Feb", "Mar", "Apr",
                                         "May", "Jun", "Jul", "Aug",
                                         "Sep", "Oct", "Nov", "Dec"};
  const char *value = "";
  if (item >= DAY_1 && item <= DAY_7)
    value = days[item - DAY_1];
  else if (item >= ABDAY_1 && item <= ABDAY_7)
    value = abdays[item - ABDAY_1];
  else if (item >= MON_1 && item <= MON_12)
    value = months[item - MON_1];
  else if (item >= ABMON_1 && item <= ABMON_12)
    value = abmonths[item - ABMON_1];
  else if (item >= ALTMON_1 && item <= ALTMON_12)
    value = months[item - ALTMON_1];
  else if (item >= ABALTMON_1 && item <= ABALTMON_12)
    value = abmonths[item - ABALTMON_1];
  else {
    switch (item) {
    case CODESET:
      // The C converter maps every byte to the same-valued Unicode scalar;
      // the encoding name does not change C classification or collation.
      value = locale.data[LC_CTYPE]->chinese_encoding ? "ISO-2022-CN"
              : locale.data[LC_CTYPE]->utf8          ? "UTF-8"
                                                    : "ISO-8859-1";
      break;
    case D_T_FMT:
      value = "%a %b %e %H:%M:%S %Y";
      break;
    case D_FMT:
      value = "%m/%d/%y";
      break;
    case T_FMT:
      value = "%H:%M:%S";
      break;
    case T_FMT_AMPM:
      value = "%I:%M:%S %p";
      break;
    case AM_STR:
      value = "AM";
      break;
    case PM_STR:
      value = "PM";
      break;
    case RADIXCHAR:
      value = ".";
      break;
    case YESEXPR:
      value = "^[yY]";
      break;
    case NOEXPR:
      value = "^[nN]";
      break;
    // Empty grouping, currency, eras and alternate digits are the actual
    // C-category values, not substitutes for unsupported named locale data.
    case THOUSEP:
    case CRNCYSTR:
    case ERA:
    case ERA_D_FMT:
    case ERA_D_T_FMT:
    case ERA_T_FMT:
    case ALT_DIGITS:
    default:
      break;
    }
  }
  return const_cast<char *>(value);
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
