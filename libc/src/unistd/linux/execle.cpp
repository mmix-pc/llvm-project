//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Linux implementation of execle
///
//===----------------------------------------------------------------------===//

#include "src/unistd/execle.h"

#include "src/__support/common.h"
#include "src/unistd/exec_args.h"
#include "src/unistd/execve.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, execle, (const char *path, const char *arg0, ...)) {
  va_list varargs;
  va_start(varargs, arg0);
  size_t argc = exec_arg_count(arg0, varargs);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvla"
#if __has_warning("-Wvla-cxx-extension")
#pragma GCC diagnostic ignored "-Wvla-cxx-extension"
#endif
  char *argv[argc + 1];
#pragma GCC diagnostic pop
  exec_arg_copy(argv, argc, arg0, varargs);
  char **envp = va_arg(varargs, char **);
  va_end(varargs);
  return LIBC_NAMESPACE::execve(path, argv, envp);
}

} // namespace LIBC_NAMESPACE_DECL
