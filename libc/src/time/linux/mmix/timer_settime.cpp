//===-- MMIX Linux timer arming ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/time/timer_settime.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/time/linux/mmix/timer_id.h"
#ifdef LIBC_MMIX_MANAGED_NOTIFICATIONS
#include "src/__support/threads/linux/mmix/notification_service.h"
#else
#include "src/__support/OSUtil/linux/syscall_wrappers/timer_settime.h"
#endif

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, timer_settime,
                   (timer_t timer, int flags,
                    const itimerspec *__restrict value,
                    itimerspec *__restrict old)) {
  timer = mmix::kernel_timer_id(timer);
#ifdef LIBC_MMIX_MANAGED_NOTIFICATIONS
  int error = mmix::set_notification_timer(timer, flags, value, old);
#else
  auto result = linux_syscalls::timer_settime(timer, flags, value, old);
  int error = result ? 0 : result.error();
#endif
  if (error)
    libc_errno = error;
  return error ? -1 : 0;
}
} // namespace LIBC_NAMESPACE_DECL
