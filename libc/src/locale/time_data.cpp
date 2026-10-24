//===-- Validated time category text tables -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/time_data.h"
#include "hdr/errno_macros.h"
#include "src/locale/category_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<TimeLocaleData> TimeLocaleData::read(const LocaleDataView &file) {
  TimeLocaleData result(file);
  constexpr unsigned REQUIRED[] = {7, 7, 12, 12, 12, 12, 2, 1,
                                   1, 1, 1,  0,  1,  1,  1, 0};
  for (unsigned field = 0; field != FIELD_COUNT; ++field) {
    uint32_t key = (field + 1) << 16;
    auto item = file.find(2, key);
    if (!item || item->kind != LocaleDataView::Kind::WORDS ||
        item->bytes.size() != 4)
      return Error(EINVAL);
    unsigned count = item->word(0).value();
    if ((REQUIRED[field] && count != REQUIRED[field]) ||
        (field == ERAS && count > 1024) ||
        (field == ALTERNATIVE_DIGITS && count > 100))
      return Error(EINVAL);
    result.counts[field] = count;
    for (unsigned i = 0; i != count; ++i) {
      auto text = file.find(2, key + i + 1);
      if (!text ||
          !locale_utf8_string(*text, field < AM_PM ? 1 : 0, size_t(-1)))
        return Error(EINVAL);
    }
  }
  return result;
}

size_t TimeLocaleData::count(Field field) const {
  return field < FIELD_COUNT ? counts[field] : 0;
}

ErrorOr<LocaleDataView::Item> TimeLocaleData::text(Field field,
                                                   size_t index) const {
  if (field >= FIELD_COUNT || index >= counts[field])
    return Error(EINVAL);
  return file.find(2, ((uint32_t(field) + 1) << 16) + uint32_t(index) + 1);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
