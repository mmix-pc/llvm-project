//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/mntent/mntent_utils.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/libc_errno.h"
#include "src/__support/str_to_integer.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
void unescape(char *field) {
  char *out = field;
  while (*field) {
    if (field[0] == '\\' && field[1] && field[2] && field[3]) {
      int code = -1;
      if (field[1] == '0' && field[2] == '4' && field[3] == '0')
        code = ' ';
      else if (field[1] == '0' && field[2] == '1' && field[3] == '1')
        code = '\t';
      else if (field[1] == '0' && field[2] == '1' && field[3] == '2')
        code = '\n';
      else if (field[1] == '1' && field[2] == '3' && field[3] == '4')
        code = '\\';
      if (code >= 0) {
        *out++ = static_cast<char>(code);
        field += 4;
        continue;
      }
    }
    *out++ = *field++;
  }
  *out = '\0';
}

bool parse(char *line, struct mntent *entry) {
  char *fields[6];
  size_t count = 0;
  while (*line) {
    while (internal::isspace(*line))
      ++line;
    if (!*line || *line == '#')
      break;
    if (count == 6)
      return false;
    fields[count++] = line;
    while (*line && !internal::isspace(*line))
      ++line;
    if (*line)
      *line++ = '\0';
  }
  if (count < 4)
    return false;
  int numbers[2] = {0, 0};
  for (size_t i = 4; i < count; ++i) {
    auto value = internal::strtointeger<int>(fields[i], 10);
    if (value.has_error() || !value.parsed_len || fields[i][value.parsed_len])
      return false;
    numbers[i - 4] = value.value;
  }
  for (size_t i = 0; i < 4; ++i)
    unescape(fields[i]);
  *entry = {fields[0], fields[1], fields[2], fields[3], numbers[0], numbers[1]};
  return true;
}
} // namespace

struct mntent *read_mount_entry(File *stream, struct mntent *entry,
                                char *&buffer, size_t &capacity,
                                MountResize resize) {
  int saved_errno = libc_errno;
  File::FileLock lock(stream);
  for (;;) {
    size_t length = 0;
    bool eof = false, embedded_null = false;
    int error = 0;
    for (;;) {
      char ch;
      auto read = stream->read_unlocked(&ch, 1);
      if (read.has_error()) {
        libc_errno = read.error;
        return nullptr;
      }
      if (!read.value) {
        eof = true;
        break;
      }
      if (ch == '\n')
        break;
      // Drain failed records so a later call cannot interpret a truncated tail
      // as a new mount entry. Dynamic reads work on nonseekable streams too.
      if (error)
        continue;
      if (capacity < 2 || length >= capacity - 1) {
        if (!resize)
          error = ERANGE;
        else if (capacity > static_cast<size_t>(-1) / 2)
          error = ENOMEM;
        else {
          size_t next = capacity ? capacity * 2 : 256;
          void *allocation = resize(buffer, next);
          if (!allocation)
            error = ENOMEM;
          else {
            buffer = static_cast<char *>(allocation);
            capacity = next;
          }
        }
        if (error)
          continue;
      }
      embedded_null |= ch == '\0';
      buffer[length++] = ch;
    }
    if (error) {
      libc_errno = error;
      return nullptr;
    }
    if (buffer && capacity)
      buffer[length] = '\0';
    if (length && !embedded_null && parse(buffer, entry)) {
      libc_errno = saved_errno;
      return entry;
    }
    if (eof) {
      libc_errno = saved_errno;
      return nullptr;
    }
  }
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
