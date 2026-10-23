//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Linux implementation for `stat` functionality via the `statx` syscall.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STAT_STAT_VIA_STATX_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STAT_STAT_VIA_STATX_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/struct_stat.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/common.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/sys/sysmacros/makedev.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

/// Populates `statbuf` via a call to the `statx` syscall.
LIBC_INLINE ErrorOr<void> stat_via_statx(int dirfd, const char *__restrict path,
                                         int flags,
                                         struct stat *__restrict statbuf) {
  kernel_statx_buf xbuf;
  ErrorOr<int> result = linux_syscalls::statx(
      dirfd, path, flags, KERNEL_STATX_BASIC_STATS_MASK, &xbuf);
  if (!result)
    return Error(result.error());

  // Validate the required representability constraints before changing output.
  constexpr uint64_t MAX_INO =
      cpp::numeric_limits<decltype(statbuf->st_ino)>::max();
  constexpr uint64_t MAX_SIZE =
      cpp::numeric_limits<decltype(statbuf->st_size)>::max();
  constexpr uint64_t MAX_BLOCKS =
      cpp::numeric_limits<decltype(statbuf->st_blocks)>::max();
  if (xbuf.stx_ino > MAX_INO || xbuf.stx_size > MAX_SIZE ||
      xbuf.stx_blocks > MAX_BLOCKS)
    return Error(EOVERFLOW);

  statbuf->st_dev = LIBC_NAMESPACE::makedev(xbuf.stx_dev_major, xbuf.stx_dev_minor);
  statbuf->st_ino = static_cast<decltype(statbuf->st_ino)>(xbuf.stx_ino);
  statbuf->st_mode = xbuf.stx_mode;
  statbuf->st_nlink = xbuf.stx_nlink;
  statbuf->st_uid = xbuf.stx_uid;
  statbuf->st_gid = xbuf.stx_gid;
  statbuf->st_rdev =
      LIBC_NAMESPACE::makedev(xbuf.stx_rdev_major, xbuf.stx_rdev_minor);
  statbuf->st_size = xbuf.stx_size;
  statbuf->st_atim.tv_sec = xbuf.stx_atime.tv_sec;
  statbuf->st_atim.tv_nsec = xbuf.stx_atime.tv_nsec;
  statbuf->st_mtim.tv_sec = xbuf.stx_mtime.tv_sec;
  statbuf->st_mtim.tv_nsec = xbuf.stx_mtime.tv_nsec;
  statbuf->st_ctim.tv_sec = xbuf.stx_ctime.tv_sec;
  statbuf->st_ctim.tv_nsec = xbuf.stx_ctime.tv_nsec;
  statbuf->st_blksize = xbuf.stx_blksize;
  statbuf->st_blocks =
      static_cast<decltype(statbuf->st_blocks)>(xbuf.stx_blocks);

  return {};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STAT_STAT_VIA_STATX_H
