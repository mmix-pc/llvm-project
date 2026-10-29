//===-- Linux secure startup descriptor protection --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STARTUP_FDS_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STARTUP_FDS_H
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/macros/config.h"
#include <linux/stat.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace startup_security {

// Runs before any dependency can allocate descriptors. Errors are fatal to
// startup; no fallback may leave a sensitive open occupying a standard slot.
template <class Syscall> long protect_standard_fds(Syscall syscall) {
#ifdef SYS_fcntl
  constexpr long fcntl_number = SYS_fcntl;
#else
  constexpr long fcntl_number = SYS_fcntl64;
#endif
  for (int fd = 0; fd != 3; ++fd) {
    long result;
    do {
      result = syscall(fcntl_number, fd, F_GETFD, 0, 0, 0, 0);
    } while (result == -EINTR);
    if (result >= 0)
      continue;
    if (result != -EBADF)
      return result;
    const char *path = fd == 0 ? "/dev/full" : "/dev/null";
    int mode = fd == 0 ? O_WRONLY : O_RDONLY;
    do {
      result = syscall(SYS_openat, AT_FDCWD, reinterpret_cast<long>(path),
                       mode | O_NOFOLLOW | O_NONBLOCK, 0, 0, 0);
    } while (result == -EINTR);
    if (result < 0)
      return result;
    if (result != fd) {
      syscall(SYS_close, result, 0, 0, 0, 0, 0);
      return -EBADF;
    }
    struct statx status;
    __builtin_memset_inline(&status, 0, sizeof(status));
    do {
      result = syscall(SYS_statx, fd, reinterpret_cast<long>(""), AT_EMPTY_PATH,
                       STATX_TYPE, reinterpret_cast<long>(&status), 0);
    } while (result == -EINTR);
    if (result >= 0 &&
        (!(status.stx_mask & STATX_TYPE) ||
         (status.stx_mode & S_IFMT) != S_IFCHR || status.stx_rdev_major != 1 ||
         status.stx_rdev_minor != (fd == 0 ? 7U : 3U)))
      result = -ENODEV;
    if (result < 0) {
      syscall(SYS_close, fd, 0, 0, 0, 0, 0); // Consumed even on EINTR.
      return result;
    }
  }
  return 0;
}

} // namespace startup_security
} // namespace LIBC_NAMESPACE_DECL
#endif
