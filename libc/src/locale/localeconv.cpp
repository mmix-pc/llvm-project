//===-- Implementation of localeconv --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/localeconv.h"

#include "src/__support/CPP/limits.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/locale/locale.h"

namespace LIBC_NAMESPACE_DECL {

static char DOT_STRING[] = ".";
static char EMPTY_STRING[] = "";

static struct lconv C_LCONV = {
    .decimal_point = DOT_STRING,
    .thousands_sep = EMPTY_STRING,
    .grouping = EMPTY_STRING,
    .mon_decimal_point = EMPTY_STRING,
    .mon_thousands_sep = EMPTY_STRING,
    .mon_grouping = EMPTY_STRING,
    .positive_sign = EMPTY_STRING,
    .negative_sign = EMPTY_STRING,
    .currency_symbol = EMPTY_STRING,
    .frac_digits = CHAR_MAX,
    .p_cs_precedes = CHAR_MAX,
    .n_cs_precedes = CHAR_MAX,
    .p_sep_by_space = CHAR_MAX,
    .n_sep_by_space = CHAR_MAX,
    .p_sign_posn = CHAR_MAX,
    .n_sign_posn = CHAR_MAX,
    .int_curr_symbol = EMPTY_STRING,
    .int_frac_digits = CHAR_MAX,
    .int_p_cs_precedes = CHAR_MAX,
    .int_n_cs_precedes = CHAR_MAX,
    .int_p_sep_by_space = CHAR_MAX,
    .int_n_sep_by_space = CHAR_MAX,
    .int_p_sign_posn = CHAR_MAX,
    .int_n_sign_posn = CHAR_MAX,
};

LLVM_LIBC_FUNCTION(struct lconv *, localeconv, ()) {
  // Query storage belongs to this thread; strings borrow the selected category.
  static LIBC_THREAD_LOCAL struct lconv result;
  result = C_LCONV;
  auto locale = internal::locale_snapshot(internal::thread_locale);
  auto text = [](const internal::LocaleDataView::Item &item) {
    return const_cast<char *>(
        reinterpret_cast<const char *>(item.bytes.data()));
  };
  auto *numeric = locale.__data[LC_NUMERIC];
  if (numeric->numeric) {
    result.decimal_point = text(numeric->numeric->decimal_point);
    result.thousands_sep = text(numeric->numeric->thousands_sep);
    result.grouping = numeric->grouping;
  }
  auto *monetary = locale.__data[LC_MONETARY];
  if (monetary->monetary) {
    const auto &data = *monetary->monetary;
    using M = internal::MonetaryLocaleData;
    result.int_curr_symbol = text(data.strings[M::INT_CURR_SYMBOL]);
    result.currency_symbol = text(data.strings[M::CURRENCY_SYMBOL]);
    result.mon_decimal_point = text(data.strings[M::DECIMAL_POINT]);
    result.mon_thousands_sep = text(data.strings[M::THOUSANDS_SEP]);
    result.mon_grouping = monetary->grouping;
    result.positive_sign = text(data.strings[M::POSITIVE_SIGN]);
    result.negative_sign = text(data.strings[M::NEGATIVE_SIGN]);
    char *fields[] = {&result.int_frac_digits,   &result.frac_digits,
                      &result.p_cs_precedes,     &result.p_sep_by_space,
                      &result.n_cs_precedes,     &result.n_sep_by_space,
                      &result.p_sign_posn,       &result.n_sign_posn,
                      &result.int_p_cs_precedes, &result.int_p_sep_by_space,
                      &result.int_n_cs_precedes, &result.int_n_sep_by_space,
                      &result.int_p_sign_posn,   &result.int_n_sign_posn};
    static_assert(sizeof(fields) / sizeof(*fields) == M::NUMBER_COUNT);
    for (unsigned i = 0; i != M::NUMBER_COUNT; ++i)
      *fields[i] = data.numbers[i] == 127 ? CHAR_MAX : char(data.numbers[i]);
  }
  return &result;
}

} // namespace LIBC_NAMESPACE_DECL
