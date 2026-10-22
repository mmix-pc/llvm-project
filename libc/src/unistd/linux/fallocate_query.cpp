//===-- Linux per-object allocation queries -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/linux/fallocate_query.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fstatfs.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include <linux/magic.h>
#include <linux/stat.h>

namespace LIBC_NAMESPACE_DECL {

ErrorOr<long> fallocate_support(int fd) {
  internal::kernel_statx_buf object = {};
  auto status =
      linux_syscalls::statx(fd, "", AT_EMPTY_PATH, STATX_TYPE, &object);
  if (!status)
    return Error(status.error());
  if (!(object.stx_mask & STATX_TYPE))
    return Error(ENOTSUP);
  // POSIX leaves association with non-regular files unspecified.
  if ((object.stx_mode & S_IFMT) != S_IFREG)
    return Error(EINVAL);

#ifndef LIBC_HAS_POSIX_FALLOCATE
  return -1L;
#else
  struct statfs fs;
  auto filesystem = linux_syscalls::fstatfs(fd, &fs);
  if (!filesystem)
    return Error(filesystem.error());
  switch (static_cast<unsigned long>(fs.f_type)) {
  case TMPFS_MAGIC:
    // shmem_file_operations provides mode-zero shmem_fallocate.
    return 1L;
  case RAMFS_MAGIC:
    // ramfs_file_operations has no fallocate operation.
    return -1L;
  default:
    // FIXME: Linux has no generic per-inode allocation-capability query.
    // In particular, ext4's magic does not identify extent-backed files.
    return Error(ENOTSUP);
  }
#endif
}

ErrorOr<long> fallocate_support(const char *path) {
  // Pin one object without read permission or device/FIFO open side effects.
  auto fd = linux_syscalls::open(path, O_PATH | O_CLOEXEC, 0);
  if (!fd)
    return Error(fd.error());
  auto result = fallocate_support(*fd);
  auto closed = linux_syscalls::close(*fd);
  // Linux consumes the descriptor even on close errors; never retry it.
  if (result && !closed)
    return Error(closed.error());
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
