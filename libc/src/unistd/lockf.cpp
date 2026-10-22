//===-- Implementation of lockf -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/lockf.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/struct_flock.h"
#include "hdr/unistd_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/unistd/getpid.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, lockf, (int fd, int cmd, off_t len)) {
  struct flock lock = {};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_CUR;
  // Let fcntl resolve the current position and validate signed/to-end ranges.
  lock.l_len = len;

  switch (cmd) {
  case F_ULOCK:
    lock.l_type = F_UNLCK;
    return LIBC_NAMESPACE::fcntl(fd, F_SETLK, &lock);
  case F_LOCK:
    // Blocking fcntl owns cancellation as well as the wait.
    return LIBC_NAMESPACE::fcntl(fd, F_SETLKW, &lock);
  case F_TLOCK:
    return LIBC_NAMESPACE::fcntl(fd, F_SETLK, &lock);
  case F_TEST:
    lock.l_type = F_RDLCK;
    if (LIBC_NAMESPACE::fcntl(fd, F_GETLK, &lock) < 0)
      return -1;
    if (lock.l_type == F_UNLCK || lock.l_pid == LIBC_NAMESPACE::getpid())
      return 0;
    libc_errno = EACCES;
    return -1;
  default:
    libc_errno = EINVAL;
    return -1;
  }
}

} // namespace LIBC_NAMESPACE_DECL
