//===-- Validated monetary category data ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/monetary_data.h"
#include "hdr/errno_macros.h"
#include "src/locale/category_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<MonetaryLocaleData>
MonetaryLocaleData::read(const LocaleDataView &file) {
  MonetaryLocaleData result{};
  for (unsigned i = 0; i != TEXT_COUNT; ++i) {
    auto item = file.find(4, i + 1);
    size_t maximum =
        (i == DECIMAL_POINT || i == THOUSANDS_SEP) ? 1 : size_t(-1);
    if (!item || !locale_utf8_string(*item, 0, maximum))
      return Error(EINVAL);
    if (i == INT_CURR_SYMBOL && item->bytes.size() != 1) {
      if (!locale_utf8_string(*item, 4, 4))
        return Error(EINVAL);
      for (unsigned j = 0; j != 3; ++j)
        if (item->bytes[j] < 'A' || item->bytes[j] > 'Z')
          return Error(EINVAL);
    }
    result.strings[i] = *item;
  }
  auto grouping = file.find(4, 7);
  if (!grouping || !locale_grouping(*grouping))
    return Error(EINVAL);
  result.grouping = *grouping;
  constexpr uint32_t LIMITS[NUMBER_COUNT] = {126, 126, 1, 2, 1, 2, 4,
                                             4,   1,   2, 1, 2, 4, 4};
  for (unsigned i = 0; i != NUMBER_COUNT; ++i) {
    auto item = file.find(4, i + 8);
    if (!item || item->kind != LocaleDataView::Kind::WORDS ||
        item->bytes.size() != 4)
      return Error(EINVAL);
    uint32_t number = item->word(0).value();
    if (number > LIMITS[i] && number != 127)
      return Error(EINVAL);
    result.numbers[i] = uint8_t(number);
  }
  return result;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
