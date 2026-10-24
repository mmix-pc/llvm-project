//===-- Validated numeric locale data -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/numeric_data.h"
#include "hdr/errno_macros.h"
#include "src/locale/category_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<NumericLocaleData> NumericLocaleData::read(const LocaleDataView &file) {
  auto decimal = file.find(CATEGORY, DECIMAL_POINT);
  auto thousands = file.find(CATEGORY, THOUSANDS_SEP);
  auto grouping = file.find(CATEGORY, GROUPING);
  if (!decimal || !thousands || !grouping ||
      !locale_utf8_string(*decimal, 1, 1) ||
      !locale_utf8_string(*thousands, 0, 1) || !locale_grouping(*grouping))
    return Error(EINVAL);
  return NumericLocaleData{*decimal, *thousands, *grouping};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
