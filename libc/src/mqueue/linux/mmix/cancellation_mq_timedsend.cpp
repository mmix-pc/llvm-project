//===-- MMIX Linux cancellable mq_timedsend -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_timedsend.h"

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/mqueue/linux/mqueue_timespec.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_timedsend,
                   (mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                    unsigned msg_prio, const struct timespec *abs_timeout)) {
  mqueue::KernelTimespec timeout = {};
  if (abs_timeout)
    timeout = {abs_timeout->tv_sec, abs_timeout->tv_nsec};

  long result = mmix::cancellation_syscall(
      SYS_mq_timedsend, mqdes, reinterpret_cast<long>(msg_ptr), msg_len,
      msg_prio, reinterpret_cast<long>(abs_timeout ? &timeout : nullptr));
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}

} // namespace LIBC_NAMESPACE_DECL
