//===-- MMIX Linux cancellable sendmmsg -----------------------------------===//
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
#include "src/sys/socket/sendmmsg.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sendmmsg,
                   (int fd, mmsghdr *messages, unsigned int count, int flags)) {
  long result = mmix::cancellation_syscall(
      SYS_sendmmsg, fd, reinterpret_cast<long>(messages), count, flags);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  for (long i = 0; i < result; ++i)
    LIBC_MSAN_UNPOISON(&messages[i].msg_len, sizeof(messages[i].msg_len));
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
