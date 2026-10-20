//===-- Directory scan ownership --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DIRENT_SCANDIR_UTILS_H
#define LLVM_LIBC_SRC_DIRENT_SCANDIR_UTILS_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/File/dir.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/openat.h"
#include "src/stdlib/qsort_util.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
namespace internal {

using DirectoryCompare = int (*)(const struct dirent **,
                                 const struct dirent **);

// Own both the descriptor and every partial result through callbacks/unwind.
struct DirectoryScan {
  int fd;
  Dir *dir = nullptr;
  struct dirent **entries = nullptr;
  size_t count = 0;
  size_t capacity = 0;

  ~DirectoryScan() {
    int saved_errno = libc_errno;
    if (dir)
      dir->close(/*consume_on_error=*/true);
    else
      platform_closedir(fd);
    for (size_t i = 0; i < count; ++i)
      ::free(entries[i]);
    ::free(entries);
    libc_errno = saved_errno;
  }

  int append(const struct dirent *entry) {
    if (count == static_cast<size_t>(cpp::numeric_limits<int>::max()))
      return EOVERFLOW;
    if (count == capacity) {
      size_t next = capacity ? capacity * 2 : 16;
      if (next > cpp::numeric_limits<size_t>::max() / sizeof(*entries))
        return ENOMEM;
      auto *storage = static_cast<struct dirent **>(
          ::realloc(entries, next * sizeof(*entries)));
      if (!storage)
        return ENOMEM;
      entries = storage;
      capacity = next;
    }
    size_t bytes = platform_dir_reclen(const_cast<struct dirent *>(entry));
    auto *copy = static_cast<struct dirent *>(::malloc(bytes));
    if (!copy)
      return ENOMEM;
    inline_memcpy(copy, entry, bytes);
    entries[count++] = copy;
    return 0;
  }
};

LIBC_INLINE int compare_directory_entries(const void *left, const void *right,
                                          void *context) {
  const struct dirent *a = *static_cast<struct dirent *const *>(left);
  const struct dirent *b = *static_cast<struct dirent *const *>(right);
  return (*static_cast<DirectoryCompare *>(context))(&a, &b);
}

LIBC_INLINE int scan_directory(int fd, const char *path, struct dirent ***list,
                               int (*filter)(const struct dirent *),
                               DirectoryCompare compare) {
  int saved_errno = libc_errno;
  int opened =
      LIBC_NAMESPACE::openat(fd, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (opened < 0)
    return -1;
  DirectoryScan scan{opened};
  auto directory = Dir::fdopen(opened);
  if (!directory) {
    libc_errno = directory.error();
    return -1;
  }
  scan.dir = directory.value();
  for (;;) {
    auto next = scan.dir->read();
    if (!next) {
      libc_errno = next.error();
      return -1;
    }
    if (!next.value())
      break;
    if (filter && !filter(next.value()))
      continue;
    int error = scan.append(next.value());
    if (error) {
      libc_errno = error;
      return -1;
    }
  }
  if (compare && scan.count > 1)
    unstable_sort(scan.entries, scan.count, sizeof(*scan.entries),
                  compare_directory_entries, &compare);
  int count = static_cast<int>(scan.count);
  *list = scan.entries;
  scan.entries = nullptr;
  scan.count = 0;
  libc_errno = saved_errno;
  return count;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
