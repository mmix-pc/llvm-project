//===-- Command substitution syntax -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_SHELL_SYNTAX_H
#define LLVM_LIBC_SRC_WORDEXP_SHELL_SYNTAX_H

#include "src/__support/wchar/restartable_converter.h"
#include "src/wordexp/syntax.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Parse a complete compound list without applying aliases or evaluating any
// expansion. No environment, process or output services are used. Source is
// decoded, immutable and contains no NUL. The closing delimiter is included
// in success.position. Unsupported lexical forms fail explicitly; this parser
// alone does not qualify the public wordexp command-substitution pipeline.
WordSyntaxResult scan_word_command(cpp::span<const uint32_t> source,
                                   size_t begin, bool backquoted,
                                   bool quoted = false);
WordCommandSyntax word_command_syntax();

// Supplies the selected encoding for dollar-quoted document delimiters. The
// service borrows this object. Stateful document byte matching still needs the
// owned-source adapter and is not admitted through scalar-only comparison.
class WordCommandParser {
  RestartableConverter converter;

public:
  explicit WordCommandParser(RestartableConverter converter)
      : converter(converter) {}
  WordCommandSyntax service();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
