//===-- Numeric conversion category selection -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/numeric_locale.h"
#ifdef LIBC_FULL_BUILD
#include "src/__support/wchar/scalar_codec.h"
#include "src/locale/locale.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {

#ifdef LIBC_FULL_BUILD
NumericParseLocale numeric_parse_locale(locale_t locale) {
  auto snapshot = locale_snapshot(locale);
  NumericParseLocale result;
  result.ctype = snapshot.__data[LC_CTYPE];
  if (const auto &data = snapshot.__data[LC_NUMERIC]->numeric) {
    auto bytes = data->decimal_point.bytes;
    result.radix = {reinterpret_cast<const char *>(bytes.data()),
                    bytes.size() - 1};
    // Category admission requires exactly one valid UTF-8 scalar.
    result.wide_radix = static_cast<wchar_t>(
        decode_scalar(ScalarEncoding::UTF8, bytes).value().value);
    auto thousands = data->thousands_sep.bytes;
    if (thousands.size() > 1) {
      result.thousands = {reinterpret_cast<const char *>(thousands.data()),
                          thousands.size() - 1};
      result.wide_thousands = static_cast<wchar_t>(
          decode_scalar(ScalarEncoding::UTF8, thousands).value().value);
    }
    result.grouping = data->grouping.bytes;
  }
  return result;
}
#endif

NumericParseLocale current_numeric_parse_locale() {
#ifdef LIBC_FULL_BUILD
  return numeric_parse_locale(thread_locale);
#else
  return {};
#endif
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
