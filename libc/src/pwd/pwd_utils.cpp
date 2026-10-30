//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Helper functions for pwd.
///
//===----------------------------------------------------------------------===//

#include "src/pwd/pwd_utils.h"
#include "hdr/errno_macros.h"
#include "hdr/func/realloc.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/struct_passwd.h"
#include "src/__support/CPP/span.h"
#include "src/__support/File/account_database.h"
#include "src/__support/char_vector.h"
#include "src/__support/libc_errno.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/string_utils.h"

#ifndef LIBC_COPT_PWD_FILE_PATH
#define LIBC_COPT_PWD_FILE_PATH "/etc/passwd"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace pwd {

ErrorOr<struct passwd> parse_passwd_line(char *line) {
  if (!line)
    return Error(EINVAL);

  struct passwd pwd;
  size_t len = internal::string_length(line);
  if (!parse_line(cpp::span<char>(line, len + 1), &pwd))
    return Error(EINVAL);

  return pwd;
}

} // namespace pwd

namespace passwd {

// As with the nonreentrant keyed queries, callers serialize enumeration.
static const char *file_path = LIBC_COPT_PWD_FILE_PATH;
static File *database = nullptr;
static internal::AccountBuffer<struct passwd> stream_buffer;
static bool discard_line = false;

namespace {
struct ErrnoGuard {
  int saved = libc_errno;
  ~ErrnoGuard() { libc_errno = saved; }
};
} // namespace

void TESTONLY_set_passwd_path(const char *path) {
  if (!path)
    return;
  close();
  file_path = path;
}

ErrorOr<void> open() {
  ErrnoGuard errno_guard;
  if (!database) {
    auto opened = openfile(file_path, "r", /*close_on_exec=*/true);
    if (!opened)
      return Error(opened.error());
    database = opened.value();
  } else {
    auto sought = database->seek(0, SEEK_SET);
    if (!sought)
      return Error(sought.error());
    database->clearerr();
  }
  discard_line = false;
  return {};
}

ErrorOr<void> close() {
  ErrnoGuard errno_guard;
  File *old = database;
  // Close consumes the stream even on error. Retain borrowed result storage.
  database = nullptr;
  discard_line = false;
  int error = old ? old->close() : 0;
  if (error)
    return Error(error);
  return {};
}

ErrorOr<struct passwd *> read_next() {
  ErrnoGuard errno_guard;
  if (!database) {
    auto opened = open();
    if (!opened)
      return Error(opened.error());
  }
  File::FileLock lock(database);
  for (;;) {
    // A failed partial read on a nonseekable stream cannot be retried. Do not
    // interpret its remaining suffix as a new passwd record.
    while (discard_line) {
      char ch;
      auto read = database->read_unlocked(&ch, 1);
      if (read.value && ch == '\n')
        discard_line = false;
      if (read.has_error())
        return Error(read.error);
      if (!read.value || ch == '\n')
        discard_line = false;
      if (!read.value)
        return nullptr;
    }
    auto start = database->tell();
    bool consumed = false;
    auto retry_error = [&](int error, bool incomplete) {
      if (start && database->seek(start.value(), SEEK_SET))
        database->clearerr_unlocked();
      else
        discard_line = consumed && incomplete;
      return Error(error);
    };
    CharVector line;
    bool eof = false, embedded_null = false;
    for (;;) {
      char ch;
      auto read = database->read_unlocked(&ch, 1);
      consumed |= read.value != 0;
      if (read.has_error())
        return retry_error(read.error, !read.value || ch != '\n');
      if (!read.value) {
        eof = true;
        break;
      }
      if (ch == '\n')
        break;
      embedded_null |= ch == '\0';
      if (!line.append(ch))
        return retry_error(ENOMEM, true);
    }
    if (!line.length() && eof)
      return nullptr;
    if (!line.length() || line.c_str()[0] == '#' || embedded_null)
      continue;
    size_t length = line.length() + 1;
    char *data = line.c_str();
    struct passwd candidate;
    if (!pwd::parse_line(cpp::span<char>(data, length), &candidate))
      continue;
    if (stream_buffer.size < length) {
      void *allocation = ::realloc(stream_buffer.buffer, length);
      if (!allocation)
        return retry_error(ENOMEM, false);
      stream_buffer.buffer = static_cast<char *>(allocation);
      stream_buffer.size = length;
    }
    // Publish only a complete valid record. Failed reads and allocations leave
    // the previous result intact, even after skipping malformed input.
    inline_memcpy(stream_buffer.buffer, data, length);
    auto relocate = [&](char *field) {
      return stream_buffer.buffer + (field - data);
    };
    candidate.pw_name = relocate(candidate.pw_name);
    candidate.pw_passwd = relocate(candidate.pw_passwd);
    candidate.pw_gecos = relocate(candidate.pw_gecos);
    candidate.pw_dir = relocate(candidate.pw_dir);
    candidate.pw_shell = relocate(candidate.pw_shell);
    stream_buffer.entry = candidate;
    return &stream_buffer.entry;
  }
}

} // namespace passwd
} // namespace LIBC_NAMESPACE_DECL
