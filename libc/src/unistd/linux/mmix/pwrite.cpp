//===-- MMIX Linux positioned write without append side effects -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/pwrite.h"

#include "hdr/types/struct_iovec.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif
#include <linux/fs.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, pwrite,
                   (int fd, const void *buffer, size_t count, off_t offset)) {
  // pwritev2 gives -1 a current-offset meaning which pwrite must not inherit.
  if (offset < 0) {
    libc_errno = EINVAL;
    return -1;
  }
  iovec vector{const_cast<void *>(buffer), count};
  // On LP64 the low argument carries all offset bits. Never toggle O_APPEND
  // on the shared description or fall back to append-sensitive pwrite64.
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  long result = mmix::cancellation_syscall(
      SYS_pwritev2, fd, reinterpret_cast<long>(&vector), 1, offset, 0,
      RWF_NOAPPEND);
#else
  long result = LIBC_NAMESPACE::syscall_impl<long>(
      SYS_pwritev2, fd, &vector, 1, offset, 0, RWF_NOAPPEND);
#endif
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
