//===-- Validated time category data ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_TIME_DATA_H
#define LLVM_LIBC_SRC_LOCALE_TIME_DATA_H

#include "src/__support/CPP/string_view.h"
#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Validated text, era dates and acyclic format references. This is not the
// formatting engine. The backing file must remain immutable.
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

  struct Date {
    int64_t year;
    unsigned month, day;
    // Zero denotes a finite date; -1/+1 denote the two open era endpoints.
    int infinity = 0;
  };
  struct Era {
    int direction;
    int64_t offset;
    Date start, end;
    cpp::string_view name, format;
  };
  static ErrorOr<Era> parse_era(cpp::string_view text);
  ErrorOr<Era> era(size_t index) const;

private:
  LocaleDataView file;
  size_t counts[FIELD_COUNT]{};
  explicit TimeLocaleData(const LocaleDataView &file) : file(file) {}
  bool valid_formats() const;
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_TIME_DATA_H
