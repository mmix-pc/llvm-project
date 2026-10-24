//===-- Validated time category text tables ----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_TIME_DATA_H
#define LLVM_LIBC_SRC_LOCALE_TIME_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Text structure and UTF-8 validation only. Era/calendar and format parsing
// must succeed before publication. The backing file must remain immutable.
class TimeLocaleData {
public:
  enum Field : unsigned {
    ABDAY,
    DAY,
    ABMON,
    MON,
    ALT_MON,
    ABALT_MON,
    AM_PM,
    DATE_TIME_FORMAT,
    DATE_FORMAT,
    TIME_FORMAT,
    AM_PM_FORMAT,
    ERAS,
    ERA_DATE_FORMAT,
    ERA_TIME_FORMAT,
    ERA_DATE_TIME_FORMAT,
    ALTERNATIVE_DIGITS,
    FIELD_COUNT
  };
  static ErrorOr<TimeLocaleData> read(const LocaleDataView &file);
  size_t count(Field field) const;
  ErrorOr<LocaleDataView::Item> text(Field field, size_t index) const;

private:
  LocaleDataView file;
  size_t counts[FIELD_COUNT]{};
  explicit TimeLocaleData(const LocaleDataView &file) : file(file) {}
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_TIME_DATA_H
