//===-- Numeric and monetary category construction ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/conventions.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool copy_grouping(__locale_data &category, const LocaleDataView::Item &item) {
  AllocChecker ac;
  category.grouping = new (ac) char[item.bytes.size()];
  if (!ac)
    return false;
  for (size_t i = 0; i != item.bytes.size(); ++i)
    category.grouping[i] =
        item.bytes[i] == 127 ? CHAR_MAX : char(item.bytes[i]);
  return true;
}
} // namespace

ErrorOr<__locale_data *> create_numeric_category(LocaleDataFile &file) {
  auto data = NumericLocaleData::read(file.view());
  if (!data)
    return Error(data.error());
  auto category = create_locale_category(file, true);
  if (!category)
    return Error(category.error());
  if (!copy_grouping(**category, data->grouping)) {
    release_locale_category(*category);
    return Error(ENOMEM);
  }
  (*category)->numeric = *data;
  return category;
}

ErrorOr<__locale_data *> create_monetary_category(LocaleDataFile &file) {
  auto data = MonetaryLocaleData::read(file.view());
  if (!data)
    return Error(data.error());
  auto category = create_locale_category(file, true);
  if (!category)
    return Error(category.error());
  if (!copy_grouping(**category, data->grouping)) {
    release_locale_category(*category);
    return Error(ENOMEM);
  }
  auto symbol = data->strings[MonetaryLocaleData::CURRENCY_SYMBOL].bytes;
  AllocChecker ac;
  // Each item is bounded by LocaleDataView::MAX_SIZE, so adding one cannot
  // wrap.
  (*category)->currency = new (ac) char[symbol.size() + 1];
  if (!ac) {
    release_locale_category(*category);
    return Error(ENOMEM);
  }
  char *currency = (*category)->currency;
  if (symbol.size() == 1) {
    currency[0] = '\0';
  } else {
    // When positive/negative placement differs, CRNCYSTR is unspecified.
    // Use the positive placement, as in the source locale's GNU convention.
    currency[0] = data->numbers[MonetaryLocaleData::P_CS_PRECEDES] ? '-' : '+';
    for (size_t i = 0; i != symbol.size(); ++i)
      currency[i + 1] = char(symbol[i]);
  }
  (*category)->monetary = *data;
  return category;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
