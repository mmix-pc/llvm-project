//===-- MMIX Linux cancellable recv ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/sys/socket/recv.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, recv,
                   (int fd, void *buffer, size_t size, int flags)) {
  long result = mmix::cancellation_syscall(
      SYS_recvfrom, fd, reinterpret_cast<long>(buffer), size, flags, 0, 0);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  LIBC_MSAN_UNPOISON(buffer, static_cast<size_t>(result) < size
                                 ? static_cast<size_t>(result)
                                 : size);
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
