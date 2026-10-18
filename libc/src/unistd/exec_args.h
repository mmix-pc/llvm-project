//===-- Argument lists for execl and execle ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UNISTD_EXEC_ARGS_H
#define LLVM_LIBC_SRC_UNISTD_EXEC_ARGS_H

#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"
#include <stdarg.h>

namespace LIBC_NAMESPACE_DECL {

// Count without consuming the caller's list. The caller keeps argv on its
// stack: execl/execle must not acquire allocator locks after fork.
inline size_t exec_arg_count(const char *arg0, va_list args) {
  va_list copy;
  va_copy(copy, args);
  size_t count = 0;
  for (const char *arg = arg0; arg; arg = va_arg(copy, const char *))
    ++count;
  va_end(copy);
  return count;
}

inline void exec_arg_copy(char **argv, size_t count, const char *arg0,
                          va_list &args) {
  if (count) {
    argv[0] = const_cast<char *>(arg0);
    // Consume the terminating null too, leaving execle's envp next.
    for (size_t i = 1; i <= count; ++i)
      argv[i] = const_cast<char *>(va_arg(args, const char *));
  } else {
    argv[0] = nullptr;
  }
}

} // namespace LIBC_NAMESPACE_DECL
#endif
