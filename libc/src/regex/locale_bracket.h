//===-- Locale bracket compiler for the private regex engine ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_REGEX_LOCALE_BRACKET_H
#define LLVM_LIBC_SRC_REGEX_LOCALE_BRACKET_H

#include "src/locale/collation_data.h"
#include "src/regex/bsd_regex.h"

struct __locale_data;

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// The compiler context is transient. Compiled sets own their syntax and borrow
// only the immutable category data, which must outlive the compiled expression.
struct RegexBracketLocale {
  const __locale_data *ctype;
  const CollationData *collation;
  int flags;
};
int compile_regex_bracket(const void *context, const llvm_regex_char_t *text,
                          size_t size, size_t *consumed,
                          llvm_regex_bracket *result);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
