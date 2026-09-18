//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_FILE_ACCOUNT_DATABASE_H
#define LLVM_LIBC_SRC___SUPPORT_FILE_ACCOUNT_DATABASE_H

#include "hdr/errno_macros.h"
#include "hdr/func/realloc.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Parse returns EINVAL for a malformed record, ERANGE for insufficient space.
template <typename Entry, typename Parse, typename Match>
int lookup_account(const char *path, Entry *entry, char *buffer, size_t size,
                   Entry **result, Parse parse, Match match) {
  if (!result)
    return EINVAL;
  *result = nullptr;
  if (!entry || !buffer)
    return EINVAL;
  if (!size)
    return ERANGE;
  struct ErrnoGuard {
    int saved = libc_errno;
    ~ErrnoGuard() { libc_errno = saved; }
  } errno_guard;
  auto opened = openfile(path, "r");
  if (!opened)
    return opened.error();
  struct FileGuard {
    File *file;
    ~FileGuard() { file->close(); }
  } guard{opened.value()};
  for (;;) {
    size_t length = 0;
    bool eof = false;
    bool embedded_null = false;
    for (;;) {
      char ch;
      auto read = guard.file->read(&ch, 1);
      if (read.has_error())
        return read.error;
      if (!read.value) {
        eof = true;
        break;
      }
      if (ch == '\n')
        break;
      if (length == size - 1)
        return ERANGE;
      embedded_null |= ch == '\0';
      buffer[length++] = ch;
    }
    buffer[length] = '\0';
    Entry candidate;
    if (length && buffer[0] != '#' && !embedded_null) {
      int error = parse(buffer, length + 1, size, &candidate);
      if (error == ERANGE)
        return error;
      if (!error && match(candidate)) {
        *entry = candidate;
        *result = entry;
        return 0;
      }
    }
    if (eof)
      return 0;
  }
}

// Nonreentrant results are process-owned and may be overwritten by the next
// query in the same database family. Keep the allocation for process lifetime.
template <typename Entry> struct AccountBuffer {
  Entry entry{};
  char *buffer = nullptr;
  size_t size = 0;

  template <typename Query, typename Resize>
  Entry *lookup(Query query, Resize resize) {
    int saved_errno = libc_errno;
    for (;;) {
      if (size) {
        Entry *result = nullptr;
        int error = query(&entry, buffer, size, &result);
        if (error != ERANGE) {
          libc_errno = error ? error : saved_errno;
          return error ? nullptr : result;
        }
      }
      if (size > static_cast<size_t>(-1) / 2) {
        libc_errno = ENOMEM;
        return nullptr;
      }
      size_t next = size ? size * 2 : 256;
      void *allocation = resize(buffer, next);
      if (!allocation) {
        libc_errno = ENOMEM;
        return nullptr;
      }
      buffer = static_cast<char *>(allocation);
      size = next;
    }
  }

  template <typename Query> Entry *lookup(Query query) {
    return lookup(
        query, [](void *ptr, size_t bytes) { return ::realloc(ptr, bytes); });
  }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
