//===-- MMIX Linux cancellable sendto -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/sys/socket/sendto.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, sendto,
                   (int fd, const void *buffer, size_t size, int flags,
                    const sockaddr *address, socklen_t address_size)) {
  long result = mmix::cancellation_syscall(
      SYS_sendto, fd, reinterpret_cast<long>(buffer), size, flags,
      reinterpret_cast<long>(address), address_size);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
