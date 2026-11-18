// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/mntent/addmntent.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/snprintf.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, addmntent, (::FILE *stream, const struct mntent *entry)) {
  if (!stream || !entry || !entry->mnt_fsname || !entry->mnt_dir ||
      !entry->mnt_type || !entry->mnt_opts) {
    libc_errno = EINVAL;
    return 1;
  }
  auto *file = reinterpret_cast<File *>(stream);
  File::FileLock lock(file);
  auto seek = file->seek(0, SEEK_END);
  if (!seek) {
    libc_errno = seek.error();
    return 1;
  }
  auto write = [file](const char *text, size_t size) {
    auto result = file->write_unlocked(text, size);
    if (result.has_error())
      libc_errno = result.error;
    return !result.has_error() && result.value == size;
  };
  const char *fields[] = {entry->mnt_fsname, entry->mnt_dir, entry->mnt_type,
                          entry->mnt_opts};
  for (const char *field : fields) {
    for (; *field; ++field) {
      const char *escaped = nullptr;
      switch (*field) {
      case ' ': escaped = "\\040"; break;
      case '\t': escaped = "\\011"; break;
      case '\n': escaped = "\\012"; break;
      case '\\': escaped = "\\134"; break;
      }
      if (!write(escaped ? escaped : field, escaped ? 4 : 1))
        return 1;
    }
    if (!write(" ", 1))
      return 1;
  }
  char numbers[64];
  int length = LIBC_NAMESPACE::snprintf(numbers, sizeof(numbers), "%d %d\n",
                                       entry->mnt_freq, entry->mnt_passno);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(numbers))
    return 1;
  return write(numbers, static_cast<size_t>(length)) ? 0 : 1;
}
} // namespace LIBC_NAMESPACE_DECL
