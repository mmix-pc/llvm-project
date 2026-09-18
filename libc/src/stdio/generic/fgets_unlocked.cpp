//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/fgets_unlocked.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, fgets_unlocked,
                   (char *__restrict str, int count,
                    ::FILE *__restrict raw_stream)) {
  if (count < 1)
    return nullptr;
  auto *stream = reinterpret_cast<File *>(raw_stream);
  int i = 0;
  while (i < count - 1) {
    unsigned char c;
    auto result = stream->read_unlocked(&c, 1);
    if (result.has_error()) {
      libc_errno = result.error;
      return nullptr;
    }
    if (result.value != 1) {
      if (i == 0)
        return nullptr;
      break;
    }
    str[i++] = c;
    if (c == '\n')
      break;
  }
  str[i] = '\0';
  return str;
}
} // namespace LIBC_NAMESPACE_DECL
