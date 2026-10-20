//===-- MMIX Linux cancellable accept4 ------------------------------------===//
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
#include "src/sys/socket/accept4.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, accept4,
                   (int fd, sockaddr *__restrict address,
                    socklen_t *__restrict address_size, int flags)) {
  socklen_t capacity = address && address_size ? *address_size : 0;
  long result = mmix::cancellation_syscall(
      SYS_accept4, fd, reinterpret_cast<long>(address),
      reinterpret_cast<long>(address_size), flags);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  if (address && address_size) {
    LIBC_MSAN_UNPOISON(address_size, sizeof(*address_size));
    LIBC_MSAN_UNPOISON(address, *address_size < capacity ? *address_size
                                                       : capacity);
  }
  (void)capacity;
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
