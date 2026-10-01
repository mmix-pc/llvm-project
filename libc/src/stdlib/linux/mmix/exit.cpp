//===-- MMIX Linux normal termination -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/exit.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/main_thread.h"
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <sys/syscall.h>
#endif
#ifdef LIBC_MMIX_CONCURRENT_STDIO
#include "src/stdio/fflush.h"
#endif

namespace LIBC_NAMESPACE_DECL {

extern "C" void __cxa_finalize(void *);

namespace internal {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
static LIBC_CONSTINIT LIBC_THREAD_LOCAL bool owns_process_cleanup = false;
static mmix::LifecycleWord process_exit_wait;
#endif

[[noreturn]] void exit_process(int status) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  uint64_t blocked = UINT64_MAX, previous;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&blocked),
                   reinterpret_cast<long>(&previous), sizeof(blocked)) != 0)
    __builtin_trap();
  // Recursive exit from a destructor or process callback must not replay it
  // or deadlock on the callback list. Immediate termination skips the rest.
  if (owns_process_cleanup)
    internal::exit(status);
  bool owner;
  {
    mmix::ThreadRegistryLock lock(mmix::thread_registry);
    owner = lock.claim_process_cleanup();
  }
  if (!owner) {
    // The winner never releases ownership: exit_group terminates every peer.
    // Wait without registry/stream locks and without busy-spinning.
    for (;;) {
      auto waited = process_exit_wait.value.wait(0);
      if (!waited && waited.error() != EINTR && waited.error() != EAGAIN)
        __builtin_trap();
    }
  }
  owns_process_cleanup = true;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&previous), 0, sizeof(previous)) != 0)
    __builtin_trap();
#endif
  cleanup_current_thread();
  __cxa_finalize(nullptr);
#ifdef LIBC_MMIX_CONCURRENT_STDIO
  fflush(nullptr);
#endif
  internal::exit(status);
}
} // namespace internal

[[noreturn]] LLVM_LIBC_FUNCTION(void, exit, (int status)) {
  internal::exit_process(status);
}

} // namespace LIBC_NAMESPACE_DECL
