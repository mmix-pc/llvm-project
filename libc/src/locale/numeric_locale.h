//===-- Borrowed numeric locale selection -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_NUMERIC_LOCALE_H
#define LLVM_LIBC_SRC_LOCALE_NUMERIC_LOCALE_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/span.h"
#include "src/__support/CPP/string_view.h"
#include "src/locale/character.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// A call borrows immutable categories without changing thread selection.
struct NumericParseLocale {
  const __locale_data *ctype = nullptr;
  cpp::string_view radix = ".";
  wchar_t wide_radix = L'.';
  cpp::string_view thousands;
  wchar_t wide_thousands = L'\0';
  cpp::span<const uint8_t> grouping;
};
NumericParseLocale current_numeric_parse_locale();
#ifdef LIBC_FULL_BUILD
NumericParseLocale numeric_parse_locale(locale_t locale);
#endif

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
