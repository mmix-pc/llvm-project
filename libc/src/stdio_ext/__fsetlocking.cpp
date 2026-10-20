//===-- Implementation of __fsetlocking ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio_ext/__fsetlocking.h"
#include "include/llvm-libc-macros/stdio-ext-macros.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, __fsetlocking, (::FILE *stream, int type)) {
  auto *file = reinterpret_cast<File *>(stream);
  int previous = file->uses_internal_locking() ? FSETLOCKING_INTERNAL
                                              : FSETLOCKING_BYCALLER;
  if (type != FSETLOCKING_QUERY)
    file->set_internal_locking(type != FSETLOCKING_BYCALLER);
  return previous;
}
} // namespace LIBC_NAMESPACE_DECL
