//===-- Implementation of __fpurge ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio_ext/__fpurge.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, __fpurge, (::FILE * stream)) {
  reinterpret_cast<File *>(stream)->purge_unlocked();
}
} // namespace LIBC_NAMESPACE_DECL
