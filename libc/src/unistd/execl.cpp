//===-- Implementation of execl -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/execl.h"
#include "src/__support/common.h"
#include "src/unistd/environ.h"
#include "src/unistd/exec_args.h"
#include "src/unistd/execve.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, execl, (const char *path, const char *arg0, ...)) {
  va_list args;
  va_start(args, arg0);
  size_t count = exec_arg_count(arg0, args);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvla"
#if __has_warning("-Wvla-cxx-extension")
#pragma GCC diagnostic ignored "-Wvla-cxx-extension"
#endif
  char *argv[count + 1];
#pragma GCC diagnostic pop
  exec_arg_copy(argv, count, arg0, args);
  va_end(args);
  return LIBC_NAMESPACE::execve(path, argv, LIBC_NAMESPACE::environ);
}
} // namespace LIBC_NAMESPACE_DECL
