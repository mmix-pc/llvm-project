//===-- Linux implementation of popen -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/stdio/popen.h"
#include "src/__support/common.h"
#include "src/stdio/linux/pipe_file.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(::FILE *, popen, (const char *command, const char *mode)) {
  return reinterpret_cast<::FILE *>(open_pipe(command, mode));
}
} // namespace LIBC_NAMESPACE_DECL
