//===-- Parameter expansion operators ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_PARAMETER_H
#define LLVM_LIBC_SRC_WORDEXP_PARAMETER_H

#include "src/__support/CPP/string_view.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct ParameterExpansion {
  enum class Operation {
    VALUE,
    LENGTH,
    DEFAULT,
    ASSIGN,
    ERROR,
    ALTERNATIVE,
    PREFIX,
    LONGEST_PREFIX,
    SUFFIX,
    LONGEST_SUFFIX
  };
  enum class Action {
    VALUE,
    LENGTH,
    EMPTY,
    WORD,
    ASSIGN_WORD,
    ERROR_WORD,
    PATTERN
  };
  cpp::string_view name;
  cpp::string_view word;
  Operation operation;
  bool test_null;

  Action select(bool is_set, bool is_empty) const;
};

// The enclosing lexer supplies the complete contents of ${...}, retaining the
// original quotes/substitutions in word. This parser never expands that word;
// the caller selects a branch before evaluation and owns all borrowed bytes.
// Positional/special parameters are outside wordexp's required contract.
ErrorOr<ParameterExpansion> parse_word_parameter(cpp::string_view contents);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
