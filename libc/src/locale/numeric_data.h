//===-- Validated numeric locale data ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_NUMERIC_DATA_H
#define LLVM_LIBC_SRC_LOCALE_NUMERIC_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Views retain no ownership. Their LocaleDataFile must outlive every use.
// Text is normalized UTF-8, not necessarily the selected LC_CTYPE encoding.
struct NumericLocaleData {
  static constexpr unsigned CATEGORY = 1;
  enum Key : uint32_t { DECIMAL_POINT = 1, THOUSANDS_SEP = 2, GROUPING = 3 };
  LocaleDataView::Item decimal_point;
  LocaleDataView::Item thousands_sep;
  LocaleDataView::Item grouping;

  static ErrorOr<NumericLocaleData> read(const LocaleDataView &file);
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_NUMERIC_DATA_H
