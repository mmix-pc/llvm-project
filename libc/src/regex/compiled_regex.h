//===-- Owned public regex state --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_REGEX_COMPILED_REGEX_H
#define LLVM_LIBC_SRC_REGEX_COMPILED_REGEX_H

#include "src/regex/character_regex.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class CompiledRegex {
  // Declared before the engine so category storage outlives its callbacks.
  struct Categories {
    __locale_data *ctype = nullptr;
    __locale_data *collate = nullptr;
    ~Categories();
  } categories;
  CharacterRegex expression;
  RestartableConverter converter{true};

public:
  CompiledRegex() = default;
  CompiledRegex(const CompiledRegex &) = delete;
  CompiledRegex &operator=(const CompiledRegex &) = delete;
  int compile(cpp::string_view pattern, int flags);
  int execute(cpp::string_view input, size_t count, regmatch_t *matches,
              int flags) const {
    return expression.execute(input, count, matches, flags, converter);
  }
  size_t subexpressions() const { return expression.subexpressions(); }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
