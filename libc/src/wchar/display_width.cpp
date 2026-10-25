//===-- Locale-aware display width ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/display_width.h"
#include "hdr/types/size_t.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

int display_width(wchar_t value, const __locale_data *category) {
  if (value == 0)
    return 0;
  if (!classify_wide(value, WCTYPE_PRINT, category))
    return -1;
  struct Range {
    unsigned first, last;
    int width;
  };
  static constexpr Range RANGES[] = {
#include "src/wchar/width_table.inc"
  };
  unsigned scalar = static_cast<unsigned>(value);
  size_t first = 0, last = sizeof(RANGES) / sizeof(*RANGES);
  while (first != last) {
    size_t middle = first + (last - first) / 2;
    const auto &range = RANGES[middle];
    if (scalar < range.first)
      last = middle;
    else if (scalar > range.last)
      first = middle + 1;
    else
      return range.width;
  }
  return 1;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
