//===-- Internal word expansion evaluation ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_EXPAND_H
#define LLVM_LIBC_SRC_WORDEXP_EXPAND_H

#include "src/wordexp/arithmetic.h"
#include "src/wordexp/fragments.h"
#include "src/wordexp/pattern.h"
#include "src/wordexp/syntax.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct WordVariable {
  bool set;
  cpp::string_view value;
};

struct WordExpansionServices {
  void *context = nullptr;
  // Returned bytes are borrowed until the next service call. Evaluation makes
  // its own copy before a nested expansion can assign another variable.
  ErrorOr<WordVariable> (*lookup)(void *, cpp::string_view) = nullptr;
  int (*assign)(void *, cpp::string_view, cpp::string_view) = nullptr;
  ErrorOr<WordVariable> (*home)(void *, cpp::string_view) = nullptr;
  int (*command)(void *, cpp::span<const uint32_t>, bool backquoted,
                 CharVector &) = nullptr;
  int (*diagnostic)(void *, cpp::string_view name,
                    cpp::string_view message) = nullptr;
  WordCommandSyntax commands;
  ArithmeticVariables arithmetic;
};

enum class WordExpansionError {
  NONE,
  SYNTAX,
  UNDEFINED,
  PARAMETER_ERROR,
  SERVICE_REQUIRED,
  SYSTEM
};
struct WordExpansionResult {
  WordExpansionError error = WordExpansionError::NONE;
  WordSyntaxResult syntax;
  int system_error = 0;
  explicit operator bool() const { return error == WordExpansionError::NONE; }
};

struct WordExpansionSink {
  void *context;
  // Called once per source word, before field splitting or pathname expansion.
  // The fragments are borrowed only for this call.
  int (*word)(void *, cpp::span<const ExpansionPart>);
};

// Input is decoded, immutable and contains no NUL. Category views must remain
// alive for the call. Validate all source syntax/NOCMD before service calls;
// evaluate selected parameter alternatives only. This is an internal pipeline,
// not a substitute for the public flag/error/result and process contracts.
WordExpansionResult expand_word_fragments(cpp::span<const uint32_t> input,
                                          bool no_command, bool undef,
                                          RestartableConverter converter,
                                          const __locale_data *ctype,
                                          const CollationData *collation,
                                          WordExpansionServices services,
                                          WordExpansionSink sink);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
