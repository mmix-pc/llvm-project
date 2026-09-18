//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/fputs_unlocked.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fputs_unlocked,
                   (const char *__restrict str, ::FILE *__restrict stream)) {
  cpp::string_view text(str);
  auto result =
      reinterpret_cast<File *>(stream)->write_unlocked(str, text.size());
  if (result.has_error()) {
    libc_errno = result.error;
    return EOF;
  }
  return result.value == text.size() ? 0 : EOF;
}
} // namespace LIBC_NAMESPACE_DECL
