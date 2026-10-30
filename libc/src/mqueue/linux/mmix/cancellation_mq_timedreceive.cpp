//===-- MMIX Linux cancellable mq_timedreceive ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_timedreceive.h"

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/mqueue/linux/mqueue_timespec.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, mq_timedreceive,
                   (mqd_t mqdes, char *__restrict msg_ptr, size_t msg_len,
                    unsigned *__restrict msg_prio,
                    const struct timespec *__restrict abs_timeout)) {
  mqueue::KernelTimespec timeout = {};
  if (abs_timeout)
    timeout = {abs_timeout->tv_sec, abs_timeout->tv_nsec};

  long result = mmix::cancellation_syscall(
      SYS_mq_timedreceive, mqdes, reinterpret_cast<long>(msg_ptr), msg_len,
      reinterpret_cast<long>(msg_prio),
      reinterpret_cast<long>(abs_timeout ? &timeout : nullptr));
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  LIBC_MSAN_UNPOISON(msg_ptr, result);
  if (msg_prio)
    LIBC_MSAN_UNPOISON(msg_prio, sizeof(*msg_prio));
  return static_cast<ssize_t>(result);
}

} // namespace LIBC_NAMESPACE_DECL
