//===-- MMIX Linux timer creation ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/timer_create.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/timer_create.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/signal/linux/mmix/private_signals.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, timer_create,
                   (clockid_t clockid, sigevent *__restrict sevp,
                    timer_t *__restrict timerid)) {
  if (sevp &&
      (sevp->sigev_notify == SIGEV_SIGNAL ||
       sevp->sigev_notify == SIGEV_THREAD_ID) &&
      mmix::private_signal(sevp->sigev_signo)) {
    libc_errno = EINVAL;
    return -1;
  }
  auto result = linux_syscalls::timer_create(clockid, sevp, timerid);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
