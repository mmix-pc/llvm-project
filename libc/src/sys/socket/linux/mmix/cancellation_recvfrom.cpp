//===-- MMIX Linux cancellable recvfrom -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/socket/recvfrom.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, recvfrom,
                   (int fd, void *__restrict buffer, size_t size, int flags,
                    sockaddr *__restrict address,
                    socklen_t *__restrict address_size)) {
  size_t capacity = address && address_size ? *address_size : 0;
  long result = mmix::cancellation_syscall(
      SYS_recvfrom, fd, reinterpret_cast<long>(buffer), size, flags,
      reinterpret_cast<long>(address), reinterpret_cast<long>(address_size));
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  // MSG_TRUNC may report the original datagram size, not the bytes copied.
  LIBC_MSAN_UNPOISON(buffer, static_cast<size_t>(result) < size
                                 ? static_cast<size_t>(result)
                                 : size);
  if (address && address_size) {
    LIBC_MSAN_UNPOISON(address_size, sizeof(*address_size));
    LIBC_MSAN_UNPOISON(address, *address_size < capacity ? *address_size
                                                        : capacity);
  }
  (void)capacity;
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
