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
  else {
    switch (item) {
    case CODESET:
      value = locale_snapshot(loc).data[LC_CTYPE]->utf8 ? "UTF-8"
                                                        : "ANSI_X3.4-1968";
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
