//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PWD_LINUX_MMIX_LOOKUP_H
#define LLVM_LIBC_SRC_PWD_LINUX_MMIX_LOOKUP_H

#include "src/__support/libc_errno.h"
#include "src/pwd/pwd_utils.h"

#ifndef LIBC_COPT_PWD_FILE_PATH
#define LIBC_COPT_PWD_FILE_PATH "/etc/passwd"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace pwd {

template <typename Match>
int lookup_passwd(Match match, struct passwd *entry, char *buffer, size_t size,
                  struct passwd **result,
                  const char *path = LIBC_COPT_PWD_FILE_PATH) {
  if (!result)
    return EINVAL;
  *result = nullptr;
  if (!entry || !buffer)
    return EINVAL;
  if (size == 0)
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
    // Keep each query independent of the process-global getpwent cursor.
    for (;;) {
      char ch;
      auto read = guard.file->read(&ch, 1);
      if (read.has_error())
        return read.error;
      if (read.value == 0) {
        eof = true;
        break;
      }
      if (ch == '\n')
        break;
      if (length == size - 1)
        return ERANGE;
      buffer[length++] = ch;
    }
    buffer[length] = '\0';
    struct passwd candidate;
    if (length && buffer[0] != '#' &&
        parse_line(cpp::span<char>(buffer, length + 1), &candidate) &&
        match(candidate)) {
      *entry = candidate;
      *result = entry;
      return 0;
    }
    if (eof)
      return 0;
  }
}

} // namespace pwd
} // namespace LIBC_NAMESPACE_DECL
#endif
