//===-- Shared locale field validation --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_CATEGORY_VALIDATION_H
#define LLVM_LIBC_SRC_LOCALE_CATEGORY_VALIDATION_H

#include "src/__support/wchar/character_converter.h"
#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

LIBC_INLINE bool locale_utf8_string(const LocaleDataView::Item &item,
                                    size_t minimum, size_t maximum) {
  if (item.kind != LocaleDataView::Kind::STRING)
    return false;
  mbstate state{};
  CharacterConverter converter(&state, true);
  size_t count = 0;
  for (size_t i = 0; i + 1 < item.bytes.size(); ++i) {
    if (converter.push(char8_t(item.bytes[i])) != 0)
      return false;
    if (converter.isFull()) {
      if (++count > maximum)
        return false;
      converter.clear();
    }
  }
  return converter.isEmpty() && count >= minimum;
}

LIBC_INLINE bool locale_grouping(const LocaleDataView::Item &item) {
  if (item.kind != LocaleDataView::Kind::BYTES || item.bytes.empty() ||
      item.bytes.back() != 0)
    return false;
  // Private sentinels: zero repeats the last width, 127 stops grouping.
  // Translate 127 to the target's CHAR_MAX when constructing public lconv.
  for (size_t i = 0; i + 1 < item.bytes.size(); ++i) {
    uint8_t width = item.bytes[i];
    if (width == 0 || width > 127 ||
        (width == 127 && i + 2 != item.bytes.size()))
      return false;
  }
  return true;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_CATEGORY_VALIDATION_H
