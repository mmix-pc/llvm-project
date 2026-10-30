//===-- Linux implementation of mq_timedsend ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_timedsend.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/mqueue/linux/mqueue_timespec.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_timedsend,
                   (mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                    unsigned msg_prio, const struct timespec *abs_timeout)) {
  mqueue::KernelTimespec timeout = {};
  if (abs_timeout)
    timeout = {abs_timeout->tv_sec, abs_timeout->tv_nsec};

#if defined(SYS_mq_timedsend_time64)
  constexpr long syscall_number = SYS_mq_timedsend_time64;
#elif defined(SYS_mq_timedsend)
  constexpr long syscall_number = SYS_mq_timedsend;
#else
#error "No Linux mq_timedsend syscall is available."
#endif
  int ret = LIBC_NAMESPACE::syscall_impl<int>(syscall_number, mqdes, msg_ptr,
                                              msg_len, msg_prio,
                                              abs_timeout ? &timeout : nullptr);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
