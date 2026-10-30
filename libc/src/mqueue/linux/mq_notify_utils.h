//===-- Linux message queue notification -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_LINUX_MQ_NOTIFY_UTILS_H
#define LLVM_LIBC_SRC_MQUEUE_LINUX_MQ_NOTIFY_UTILS_H

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/linux/kernel_sigevent.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/mqueue/types.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mqueue {

LIBC_INLINE int notify(mqd_t mqdes, const struct sigevent *notification) {
  linux_syscalls::KernelSigevent event{};
  if (notification) {
    // FIXME: SIGEV_THREAD needs a managed callback service. Linux interprets
    // this mode as a netlink cookie, not a user callback.
    if (notification->sigev_notify == SIGEV_THREAD) {
      libc_errno = ENOTSUP;
      return -1;
    }
    if (notification->sigev_notify != SIGEV_NONE &&
        notification->sigev_notify != SIGEV_SIGNAL) {
      libc_errno = EINVAL;
      return -1;
    }
    // SIGEV_NONE does not require initialized signal or value fields.
    event.sigev_notify = notification->sigev_notify;
    if (event.sigev_notify == SIGEV_SIGNAL) {
      // Older Linux kernels accepted signal zero as a no-notification mode.
      if (notification->sigev_signo <= 0) {
        libc_errno = EINVAL;
        return -1;
      }
      event.sigev_signo = notification->sigev_signo;
      event.sigev_value = notification->sigev_value;
    }
  }
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_mq_notify, mqdes,
                                            notification ? &event : nullptr);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace mqueue
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_MQUEUE_LINUX_MQ_NOTIFY_UTILS_H
