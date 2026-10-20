//===-- MMIX Linux cancellable recvmsg ------------------------------------===//
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
#include "src/sys/socket/recvmsg.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, recvmsg, (int fd, msghdr *message, int flags)) {
  size_t name_capacity = message ? message->msg_namelen : 0;
  size_t control_capacity = message ? message->msg_controllen : 0;
  long result = mmix::cancellation_syscall(
      SYS_recvmsg, fd, reinterpret_cast<long>(message), flags);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  LIBC_MSAN_UNPOISON(&message->msg_namelen, sizeof(message->msg_namelen));
  LIBC_MSAN_UNPOISON(&message->msg_controllen, sizeof(message->msg_controllen));
  LIBC_MSAN_UNPOISON(&message->msg_flags, sizeof(message->msg_flags));
  LIBC_MSAN_UNPOISON(message->msg_name,
                    message->msg_namelen < name_capacity ? message->msg_namelen
                                                        : name_capacity);
  LIBC_MSAN_UNPOISON(message->msg_control,
                    message->msg_controllen < control_capacity
                        ? message->msg_controllen
                        : control_capacity);
  size_t remaining = static_cast<size_t>(result);
  for (size_t i = 0; i < message->msg_iovlen && remaining; ++i) {
    size_t size = message->msg_iov[i].iov_len < remaining
                      ? message->msg_iov[i].iov_len
                      : remaining;
    LIBC_MSAN_UNPOISON(message->msg_iov[i].iov_base, size);
    remaining -= size;
  }
  (void)name_capacity;
  (void)control_capacity;
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
