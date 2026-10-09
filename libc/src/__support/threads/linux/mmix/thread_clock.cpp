//===-- MMIX Linux leased CPU clock acquisition ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "thread_clock.h"
#include "hdr/signal_macros.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int thread_clock(ThreadAttributes *handle, clockid_t &clock) {
  if (!handle)
    return ESRCH;
  uint64_t blocked = UINT64_MAX, saved;
  long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &blocked,
                                   &saved, sizeof(saved));
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
  clockid_t candidate = 0;
  if (tid) {
    error = encode_thread_clock(tid, candidate);
    if (!error) {
      error = probe_thread_clock(candidate);
      if (error == EINVAL)
        error = ESRCH;
    }
    ThreadRegistryLock lock(thread_registry);
    if (!lock.release_lease(*control) || !lock.unpin(*control))
      __builtin_trap();
  }
  if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved, 0,
                         sizeof(saved)) != 0)
    __builtin_trap();
  // No reference survives acquisition; future clock use has kernel TID
  // lifetime.
  if (!error)
    clock = candidate;
  return error;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
