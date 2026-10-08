//===-- MMIX Linux leased scheduling --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "thread_scheduling.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int schedule_thread(ThreadAttributes *handle, int &policy, int &priority,
                    bool write, bool priority_only) {
  if (!handle)
    return ESRCH;
  uint64_t blocked = UINT64_MAX, saved;
  long result = syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                             reinterpret_cast<long>(&blocked),
                             reinterpret_cast<long>(&saved), sizeof(saved));
  if (result < 0)
    return int(-result);
  if (result)
    __builtin_trap();
  ThreadControl *control;
  int tid = 0;
  {
    ThreadRegistryLock lock(thread_registry);
    control = lock.pin(handle);
    if (control) {
      if (lock.acquire_lease(*control)) {
        tid = control->attributes.tid;
        if (tid <= 0)
          __builtin_trap();
      } else if (!lock.unpin(*control))
        __builtin_trap();
    }
  }
  int error = ESRCH;
  if (tid) {
    error = write ? scheduling_write(control->priority_protection, tid, policy,
                                     priority, priority_only)
                  : scheduling_read(control->priority_protection, tid, policy,
                                    priority);
    ThreadRegistryLock lock(thread_registry);
    if (!lock.release_lease(*control) || !lock.unpin(*control))
      __builtin_trap();
  }
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&saved), 0, sizeof(saved)) != 0)
    __builtin_trap();
  return error;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
