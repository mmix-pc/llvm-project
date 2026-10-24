//===-- Validated monetary category data ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_MONETARY_DATA_H
#define LLVM_LIBC_SRC_LOCALE_MONETARY_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Non-owning normalized UTF-8 records, not an lconv or a currency registry.
struct MonetaryLocaleData {
  enum Text : unsigned {
    INT_CURR_SYMBOL,
    CURRENCY_SYMBOL,
    DECIMAL_POINT,
    THOUSANDS_SEP,
    POSITIVE_SIGN,
    NEGATIVE_SIGN,
    TEXT_COUNT
  };
  enum Number : unsigned {
    INT_FRAC_DIGITS,
    FRAC_DIGITS,
    P_CS_PRECEDES,
    P_SEP_BY_SPACE,
    N_CS_PRECEDES,
    N_SEP_BY_SPACE,
    P_SIGN_POSN,
    N_SIGN_POSN,
    INT_P_CS_PRECEDES,
    INT_P_SEP_BY_SPACE,
    INT_N_CS_PRECEDES,
    INT_N_SEP_BY_SPACE,
    INT_P_SIGN_POSN,
    INT_N_SIGN_POSN,
    NUMBER_COUNT
  };
  LocaleDataView::Item strings[TEXT_COUNT];
  LocaleDataView::Item grouping;
  uint8_t numbers[NUMBER_COUNT];
  static ErrorOr<MonetaryLocaleData> read(const LocaleDataView &file);
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_MONETARY_DATA_H
