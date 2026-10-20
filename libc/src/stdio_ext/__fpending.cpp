//===-- Implementation of __fpending --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio_ext/__fpending.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, __fpending, (::FILE * stream)) {
  return reinterpret_cast<File *>(stream)->pending_output();
}
} // namespace LIBC_NAMESPACE_DECL
