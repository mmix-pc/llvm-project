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

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_notify,
                   (mqd_t mqdes, const struct sigevent *notification)) {
  if (notification && notification->sigev_notify == SIGEV_SIGNAL &&
      mmix::private_signal(notification->sigev_signo)) {
    libc_errno = EINVAL;
    return -1;
  }
  return mqueue::notify(mqdes, notification);
}

} // namespace LIBC_NAMESPACE_DECL
