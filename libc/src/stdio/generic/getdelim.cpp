//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/getdelim.h"
#include "hdr/func/realloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, getdelim,
                   (char **__restrict lineptr, size_t *__restrict n,
                    int delimiter, ::FILE *__restrict raw_stream)) {
  if (!raw_stream) {
    libc_errno = EINVAL;
    return -1;
  }
  auto *stream = reinterpret_cast<File *>(raw_stream);
  File::FileLock lock(stream);
  if (!lineptr || !n) {
    stream->set_error_unlocked();
    libc_errno = EINVAL;
    return -1;
  }
  size_t length = 0;
  const size_t limit = static_cast<size_t>(cpp::numeric_limits<ssize_t>::max());
  while (true) {
    if (length == limit) {
      // A maximal-length line is still representable if EOF follows.
      unsigned char extra;
      auto result = stream->read_unlocked(&extra, 1);
      if (result.has_error()) {
        libc_errno = result.error;
        return -1;
      }
      if (result.value == 0)
        return static_cast<ssize_t>(length);
      stream->set_error_unlocked();
      libc_errno = EOVERFLOW;
      return -1;
    }
    if (!*lineptr || *n < length + 2) {
      size_t capacity = *lineptr ? *n : 0;
      capacity = capacity > limit / 2 ? limit + 1 : capacity * 2;
      if (capacity < 128)
        capacity = 128;
      auto *buffer = static_cast<char *>(realloc(*lineptr, capacity));
      if (!buffer) {
        stream->set_error_unlocked();
        libc_errno = ENOMEM;
        return -1;
      }
      *lineptr = buffer;
      *n = capacity;
    }
    (*lineptr)[length] = '\0';
    unsigned char c;
    auto result = stream->read_unlocked(&c, 1);
    if (result.has_error()) {
      libc_errno = result.error;
      return -1;
    }
    if (result.value != 1)
      return length ? static_cast<ssize_t>(length) : -1;
    (*lineptr)[length++] = c;
    (*lineptr)[length] = '\0';
    if (c == static_cast<unsigned char>(delimiter))
      return static_cast<ssize_t>(length);
  }
}
} // namespace LIBC_NAMESPACE_DECL
