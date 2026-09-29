//===-- MMIX Linux internal thread signals --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_signal.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/thread.h"
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM
#error "MMIX Linux thread signals require platform threads"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static int send_signal(int tid, int signal) {
  long pid = syscall_impl(SYS_getpid);
  if (pid < 0)
    return static_cast<int>(-pid);
  long result = syscall_impl(SYS_tgkill, pid, tid, signal);
  return result < 0 ? static_cast<int>(-result) : 0;
}

int signal_thread(ThreadAttributes *handle, int signal) {
  if (signal < 0 || signal >= NSIG)
    return EINVAL;
  if (!handle)
    return ESRCH;
  // The current activation itself keeps this TID alive. A handler may exit or
  // longjmp out of delivery, so leave no reference or mask restoration behind.
  if (handle == internal::self.attrib)
    return send_signal(handle->tid, signal);

  uint64_t blocked = UINT64_MAX, saved;
  long result = syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                             reinterpret_cast<long>(&blocked),
                             reinterpret_cast<long>(&saved), sizeof(blocked));
  if (result < 0)
    return static_cast<int>(-result);
  if (result != 0)
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
    error = send_signal(tid, signal);
    ThreadRegistryLock lock(thread_registry);
    if (!lock.release_lease(*control) || !lock.unpin(*control))
      __builtin_trap();
  }
  // No control access after the last unpin, including pending local delivery.
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&saved), 0, sizeof(saved)) != 0)
    __builtin_trap();
  return error;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
