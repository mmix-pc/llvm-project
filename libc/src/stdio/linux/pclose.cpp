//===-- Linux implementation of pclose ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/stdio/pclose.h"
#include "src/__support/common.h"
#include "src/stdio/linux/pipe_file.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pclose, (::FILE * stream)) {
  return close_pipe(reinterpret_cast<File *>(stream));
}
} // namespace LIBC_NAMESPACE_DECL
