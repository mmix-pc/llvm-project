//===-- Group database stream operations ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/stream.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "hdr/stdio_macros.h"
#include "src/__support/char_vector.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace group_db {
// As with the nonreentrant keyed queries, callers serialize database iteration.
static File *database = nullptr;
static internal::AccountBuffer<struct group> stream_buffer;

ErrorOr<File *> enumeration_stream() {
  if (!database) {
    auto opened = openfile(lookup_path(), "r", /*close_on_exec=*/true);
    if (!opened)
      return Error(opened.error());
    database = opened.value();
  }
  return database;
}

int rewind_enumeration() {
  auto opened = enumeration_stream();
  if (!opened)
    return opened.error();
  auto sought = opened.value()->seek(0, SEEK_SET);
  return sought ? 0 : sought.error();
}

void close_enumeration() {
  File *old = database;
  database = nullptr;
  ::free(stream_buffer.buffer);
  stream_buffer = {};
  if (old)
    old->close();
}

int read_entry(File *file, struct group *entry, char *buffer, size_t size,
               struct group **result,
               internal::AccountBuffer<struct group> *storage) {
  if (!result)
    return EINVAL;
  *result = nullptr;
  if (!file || !entry || (!storage && !buffer))
    return EINVAL;
  if (!storage && !size)
    return ERANGE;
  struct ErrnoGuard {
    int saved = libc_errno;
    ~ErrnoGuard() { libc_errno = saved; }
  } errno_guard;
  File::FileLock lock(file);
  for (;;) {
    auto start = file->tell();
    auto retry_error = [&](int error) {
      // Pipes remain readable, but only seekable streams can retry a record.
      if (start)
        file->seek(start.value(), SEEK_SET);
      return error;
    };
    CharVector line;
    bool eof = false, embedded_null = false;
    size_t members = 1;
    for (;;) {
      char ch;
      auto read = file->read_unlocked(&ch, 1);
      if (read.has_error())
        return read.error;
      if (!read.value) {
        eof = true;
        break;
      }
      if (ch == '\n')
        break;
      embedded_null |= ch == '\0';
      members += ch == ',';
      if (!line.append(ch))
        return retry_error(ENOMEM);
    }
    if (!line.length() && eof)
      return ENOENT;
    if (!line.length() || line.c_str()[0] == '#' || embedded_null)
      continue;
    size_t length = line.length() + 1;
    if (storage) {
      // Reserve the aligned member vector before parsing mutates separators.
      size_t available = (static_cast<size_t>(-1) - length) / sizeof(char *);
      if (available < 2 || members > available - 2)
        return retry_error(ENOMEM);
      size_t required = length + (members + 2) * sizeof(char *);
      if (storage->size < required) {
        void *allocation = ::realloc(storage->buffer, required);
        if (!allocation)
          return retry_error(ENOMEM);
        storage->buffer = static_cast<char *>(allocation);
        storage->size = required;
      }
      buffer = storage->buffer;
      size = storage->size;
    }
    if (length > size)
      return retry_error(ERANGE);
    inline_memcpy(buffer, line.c_str(), length);
    int error = parse_group(buffer, length, size, entry);
    if (error == ERANGE)
      return retry_error(error);
    if (error)
      continue;
    *result = entry;
    return 0;
  }
}

struct group *read_shared(File *file) {
  struct group *result;
  int error = read_entry(file, &stream_buffer.entry, nullptr, 0, &result,
                         &stream_buffer);
  if (error && error != ENOENT)
    libc_errno = error;
  return result;
}
} // namespace group_db
} // namespace LIBC_NAMESPACE_DECL
