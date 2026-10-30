//===-- MMIX Linux message queue notification -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_notify.h"
#include "src/mqueue/linux/mq_notify_utils.h"
#include "src/signal/linux/mmix/private_signals.h"
#ifdef LIBC_MMIX_MANAGED_NOTIFICATIONS
#include "src/__support/threads/linux/mmix/notification_service.h"
#endif

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_notify,
                   (mqd_t mqdes, const struct sigevent *notification)) {
#ifdef LIBC_MMIX_MANAGED_NOTIFICATIONS
  if (notification && notification->sigev_notify == SIGEV_THREAD) {
    int error = mmix::register_mq_notification(mqdes, *notification);
    if (error)
      libc_errno = error;
    return error ? -1 : 0;
  }
#endif
  if (notification && notification->sigev_notify == SIGEV_SIGNAL &&
      mmix::private_signal(notification->sigev_signo)) {
    libc_errno = EINVAL;
    return -1;
  }
  return mqueue::notify(mqdes, notification);
}

} // namespace LIBC_NAMESPACE_DECL
