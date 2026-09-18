//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/fputc_unlocked.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fputc_unlocked, (int c, ::FILE *stream)) {
  unsigned char uc = static_cast<unsigned char>(c);
  auto result = reinterpret_cast<File *>(stream)->write_unlocked(&uc, 1);
  if (result.has_error()) {
    libc_errno = result.error;
    return EOF;
  }
  return result.value == 1 ? uc : EOF;
}
} // namespace LIBC_NAMESPACE_DECL
