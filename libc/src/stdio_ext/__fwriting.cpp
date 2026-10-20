//===-- Implementation of __fwriting --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio_ext/__fwriting.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, __fwriting, (::FILE * stream)) {
  return reinterpret_cast<File *>(stream)->writing();
}
} // namespace LIBC_NAMESPACE_DECL
