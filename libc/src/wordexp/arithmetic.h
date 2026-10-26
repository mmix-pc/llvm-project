//===-- Internal word expansion arithmetic ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_ARITHMETIC_H
#define LLVM_LIBC_SRC_WORDEXP_ARITHMETIC_H

#include "src/__support/CPP/string_view.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct ArithmeticVariables {
  void *context = nullptr;
  ErrorOr<long> (*read)(void *, cpp::string_view) = nullptr;
  int (*write)(void *, cpp::string_view, long) = nullptr;
};

// Input has already undergone shell substitutions and quote removal. This
// component never invokes a shell or accesses the process environment. Variable
// callbacks must not invalidate the input; their errors are returned unchanged.
// Validate all syntax before evaluating, including unselected operands. Only
// selected operands access variables or perform arithmetic. Overflow, invalid
// shifts and zero divisors are errors rather than host-language undefined
// behavior.
ErrorOr<long> evaluate_wordexp_arithmetic(cpp::string_view input,
                                          ArithmeticVariables variables = {});

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
