//===-- Non-cancelling Linux I/O for FTS --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_FTS_LINUX_IO_H
#define LLVM_LIBC_SRC_FTS_LINUX_IO_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/FTS.h"
#include "hdr/types/struct_dirent.h"
#include "src/__support/OSUtil/linux/stat/stat_via_statx.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fchdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/string/memset.h"
#include <dirent.h>

namespace LIBC_NAMESPACE_DECL {
namespace fts {

inline int stat_at(int fd, const char *path, int flags, struct stat *buffer) {
  auto result = internal::stat_via_statx(fd, path, flags, buffer);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}
inline int stat(const char *path, struct stat *buffer) {
  return stat_at(AT_FDCWD, path, 0, buffer);
}
inline int lstat(const char *path, struct stat *buffer) {
  return stat_at(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, buffer);
}
inline int fstat(int fd, struct stat *buffer) {
  return stat_at(fd, "", AT_EMPTY_PATH, buffer);
}
inline int fchdir(int fd) {
  auto result = linux_syscalls::fchdir(fd);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}

// These private leaves do not introduce cancellation points in the middle of
// the traversal state machine. FTS is not async-cancel-safe or signal-safe.
inline int open(const char *path, int flags, int mode = 0) {
  auto result = linux_syscalls::open(path, flags, mode);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}
inline int close(int fd) {
  auto result = linux_syscalls::close(fd);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}

// A private, single-owner getdents buffer avoids the public readdir
// cancellation policy. Kernel record bounds and NUL termination are checked
// before use.
struct Directory {
  int fd;
  size_t position;
  size_t size;
  alignas(struct dirent) unsigned char buffer[4096];
};

inline Directory *opendir(const char *path) {
  int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0)
    return nullptr;
  auto *dir = static_cast<Directory *>(::malloc(sizeof(Directory)));
  if (!dir) {
    close(fd);
    libc_errno = ENOMEM;
    return nullptr;
  }
  dir->fd = fd;
  dir->position = dir->size = 0;
  return dir;
}
inline int dirfd(Directory *dir) { return dir->fd; }
inline void closedir(Directory *dir) {
  int saved = libc_errno;
  close(dir->fd); // Never retry a consumed Linux descriptor.
  ::free(dir);
  libc_errno = saved;
}
inline struct dirent *readdir(Directory *dir) {
  if (dir->position == dir->size) {
    long result = syscall_impl<long>(SYS_getdents64, dir->fd, dir->buffer,
                                     sizeof(dir->buffer));
    if (result <= 0) {
      libc_errno = result < 0 ? static_cast<int>(-result) : 0;
      return nullptr;
    }
    if (static_cast<size_t>(result) > sizeof(dir->buffer)) {
      libc_errno = EIO;
      return nullptr;
    }
    dir->position = 0;
    dir->size = static_cast<size_t>(result);
  }
  constexpr size_t name_offset = __builtin_offsetof(struct dirent, d_name);
  size_t remaining = dir->size - dir->position;
  if (remaining < name_offset + 1) {
    libc_errno = EIO;
    return nullptr;
  }
  auto *entry = reinterpret_cast<struct dirent *>(dir->buffer + dir->position);
  size_t length = entry->d_reclen;
  if (length < name_offset + 1 || length > remaining ||
      length % alignof(struct dirent) != 0) {
    libc_errno = EIO;
    return nullptr;
  }
  size_t i = 0;
  while (i < length - name_offset && entry->d_name[i])
    ++i;
  if (i == length - name_offset) {
    libc_errno = EIO;
    return nullptr;
  }
  dir->position += length;
  return entry;
}

inline void *allocate_zero(size_t size) {
  void *p = ::malloc(size);
  if (p)
    LIBC_NAMESPACE::memset(p, 0, size);
  else
    libc_errno = ENOMEM;
  return p;
}
inline int reallocate_array(FTSENT ***array, size_t count, size_t size) {
  size_t bytes;
  if (__builtin_mul_overflow(count, size, &bytes))
    return ENOMEM;
  void *p = ::realloc(*array, bytes);
  if (!p)
    return ENOMEM;
  *array = static_cast<FTSENT **>(p);
  return 0;
}
} // namespace fts
} // namespace LIBC_NAMESPACE_DECL
#endif
