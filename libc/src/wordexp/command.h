//===-- Word substitution command capture -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_COMMAND_H
#define LLVM_LIBC_SRC_WORDEXP_COMMAND_H

#include "src/__support/CPP/string_view.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct CommandOutput {
  void *context;
  int (*append)(void *, cpp::string_view);
};

// Execute only an admitted command-substitution body. The enclosing parser
// owns syntax/NOCMD checks and removes backquote escapes before this call.
// Command and environment remain valid until spawn returns. Output is raw:
// newline removal, embedded-NUL policy and public error mapping are caller
// work. Return the reaped wait status, not the shell's exit code. A sink
// failure may leave a prefix in the caller's storage, but never transfers a
// child or fd.
ErrorOr<int> capture_word_command(const char *command, char *const *environment,
                                  bool show_error, CommandOutput output);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
