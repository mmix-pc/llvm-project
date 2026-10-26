//===-- Word expansion lexical structure ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_SYNTAX_H
#define LLVM_LIBC_SRC_WORDEXP_SYNTAX_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/span.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

enum class WordSyntaxError {
  NONE,
  BAD_CHARACTER,
  SYNTAX,
  COMMAND_FORBIDDEN,
  NESTING_LIMIT,
  COMMAND_PARSER_REQUIRED,
  RESOURCE,
  SINK_ERROR
};

struct WordSyntaxResult {
  WordSyntaxError error = WordSyntaxError::NONE;
  size_t position = 0;
  int sink_error = 0;
  explicit operator bool() const { return error == WordSyntaxError::NONE; }
};

struct WordToken {
  enum class Kind {
    LITERAL,
    PARAMETER,
    BRACED_PARAMETER,
    ARITHMETIC,
    COMMAND,
    BACKQUOTED_COMMAND,
    DOLLAR_QUOTED,
    TILDE,
    END_WORD
  };
  Kind kind;
  size_t begin, end;
  bool quoted;
};

struct WordSyntaxSink {
  void *context = nullptr;
  int (*emit)(void *, WordToken) = nullptr;
};

struct WordCommandSyntax {
  void *context = nullptr;
  // Parse only: no execution, environment writes or output callbacks. Input
  // starts after $( or `; success.position is just past the closing delimiter.
  // A shell parser must handle case patterns, comments and here-documents; a
  // parenthesis counter is not a substitute. This callback can run twice.
  WordSyntaxResult (*scan)(void *, cpp::span<const uint32_t>, size_t begin,
                           bool backquoted) = nullptr;
};

// Input is decoded by the caller, which retains the scalar-to-byte mapping.
// Token spans exclude delimiters, preserve expansion origins and borrow input.
// Name/expression spans can contain backslash-newline continuations, which the
// consumer removes before evaluation; quoted literal spans retain their bytes.
// All syntax is checked before any sink call. Nested parameter words are
// validated but not emitted: only a selected branch may later be expanded.
// Empty quoted spans are emitted so field splitting can retain empty words.
// Command syntax needs a separate parser unless commands are prohibited.
WordSyntaxResult parse_word_syntax(cpp::span<const uint32_t> input,
                                   bool no_command, WordSyntaxSink sink = {},
                                   WordCommandSyntax commands = {});

// Expandable parameter operands are one fragment, not a new top-level word
// list: spaces remain literal and no END_WORD token is emitted. Arithmetic
// operands additionally retain their operators and balanced parentheses.
WordSyntaxResult parse_word_fragment(cpp::span<const uint32_t> input,
                                    bool quoted, bool arithmetic,
                                    bool no_command, WordSyntaxSink sink = {},
                                    WordCommandSyntax commands = {});

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
