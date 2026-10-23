//===-- Implementation of fgets -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fgets.h"
#include "src/__support/File/file.h"

#include "hdr/types/FILE.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, fgets,
                   (char *__restrict str, int count,
                    ::FILE *__restrict raw_stream)) {
  if (count < 1)
    return nullptr;
  if (count == 1) {
    str[0] = '\0';
    return str;
  }

  unsigned char c = '\0';
  auto stream = reinterpret_cast<LIBC_NAMESPACE::File *__restrict>(raw_stream);
  File::FileLock lock(stream);

  // i is an int because it's frequently compared to count, which is also int.
  int i = 0;

  for (; i < (count - 1) && c != '\n'; ++i) {
    auto result = stream->read_unlocked(&c, 1);
    size_t r = result.value;
    if (result.has_error()) {
      libc_errno = result.error;
      return nullptr;
    }

    if (r != 1)
      break;
    str[i] = c;
  }

  // A retained error indicator does not make this invocation fail.
  if (i == 0 && stream->iseof_unlocked())
    return nullptr;

  str[i] = '\0';
  return str;
}

} // namespace LIBC_NAMESPACE_DECL
