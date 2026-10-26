//===-- Locale-aware public numeric conversion ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_NUMERIC_CONVERSION_H
#define LLVM_LIBC_SRC_LOCALE_NUMERIC_CONVERSION_H

#include "src/__support/str_to_float.h"
#include "src/locale/numeric_locale.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

template <typename CharType>
LIBC_INLINE size_t numeric_leading_space(const CharType *input,
                                         const NumericParseLocale &locale) {
  size_t count = 0;
  for (; input[count]; ++count) {
    if constexpr (sizeof(CharType) == sizeof(char)) {
      if (!classify_byte(static_cast<unsigned char>(input[count]), WCTYPE_SPACE,
                         locale.ctype))
        break;
    } else {
      if (!classify_wide(input[count], WCTYPE_SPACE, locale.ctype))
        break;
    }
  }
  return count;
}

template <typename T, typename CharType>
LIBC_INLINE StrToNumResult<T> locale_strtofloatingpoint(
    const CharType *input,
    NumericParseLocale locale = current_numeric_parse_locale()) {
  size_t leading = numeric_leading_space(input, locale);
  DecimalSeparator<CharType> separator;
  if constexpr (sizeof(CharType) == sizeof(char))
    separator =
        DecimalSeparator<char>(locale.radix.data(), locale.radix.size());
  else
    separator = DecimalSeparator<wchar_t>(&locale.wide_radix, 1);
  auto result = strtofloatingpoint<T>(input + leading, separator, false);
  if (result.parsed_len)
    result.parsed_len += leading;
  return result;
}

template <typename T, typename CharType>
LIBC_INLINE StrToNumResult<T> locale_strtointeger(
    const CharType *input, int base,
    NumericParseLocale locale = current_numeric_parse_locale()) {
  size_t leading = numeric_leading_space(input, locale);
  auto result = strtointeger<T>(input + leading, base,
                                cpp::numeric_limits<size_t>::max(), false);
  if (result.parsed_len)
    result.parsed_len += leading;
  return result;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
