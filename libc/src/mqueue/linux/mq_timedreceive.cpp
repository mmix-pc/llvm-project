//===-- Linux implementation of mq_timedreceive ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_timedreceive.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
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

#if defined(SYS_mq_timedreceive_time64)
  constexpr long syscall_number = SYS_mq_timedreceive_time64;
#elif defined(SYS_mq_timedreceive)
  constexpr long syscall_number = SYS_mq_timedreceive;
#else
#error "No Linux mq_timedreceive syscall is available."
#endif
  ssize_t ret = LIBC_NAMESPACE::syscall_impl<ssize_t>(
      syscall_number, mqdes, msg_ptr, msg_len, msg_prio,
      abs_timeout ? &timeout : nullptr);
  if (ret < 0) {
    libc_errno = static_cast<int>(-ret);
    return -1;
  }
  LIBC_MSAN_UNPOISON(msg_ptr, ret);
  if (msg_prio)
    LIBC_MSAN_UNPOISON(msg_prio, sizeof(*msg_prio));
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
