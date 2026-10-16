//===-- MMIX Linux signal registration ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigaction.h"
#include "rt_sigaction.h"
#include "signal_trampoline.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#endif

namespace LIBC_NAMESPACE_DECL {

// FIXME: Reuse shared signal policy after it separates the restorer/vDSO
// strategy. The single-thread profile does not yet provide the concurrent
// SIGABRT disposition and fork/spawn coordination required by pthread support.
LLVM_LIBC_FUNCTION(int, sigaction,
                   (int signal, const struct sigaction *__restrict action,
                    struct sigaction *__restrict old_action)) {
  if (signal <= 0 || signal >= NSIG
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
      || signal == 32
#endif
  ) {
    libc_errno = EINVAL;
    return -1;
  }
  struct sigaction prepared{};
  if (action) {
    prepared = *action;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
    prepared.sa_mask.__signals[0] |= uint64_t(1) << 31;
#endif
    if (!(prepared.sa_flags & SA_RESTORER)) {
      prepared.sa_flags |= SA_RESTORER;
      prepared.sa_restorer = __llvm_libc_mmix_signal_trampoline;
    }
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  loader::LoaderSignalLease lease;
  uint64_t saved_mask = 0;
  uintptr_t handler = prepared.sa_flags & SA_SIGINFO
                          ? reinterpret_cast<uintptr_t>(prepared.sa_sigaction)
                          : reinterpret_cast<uintptr_t>(prepared.sa_handler);
  bool retain = action && handler != reinterpret_cast<uintptr_t>(SIG_DFL) &&
                handler != reinterpret_cast<uintptr_t>(SIG_IGN);
  if (retain) {
    // Block interruption across the temporary lease and kernel publication.
    // The service uses only lock-free atomics and immutable leased metadata;
    // sigaction remains usable from a handler, including loader interruption.
    uint64_t full = UINT64_MAX;
    long error = syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &full,
                                    &saved_mask, sizeof(full));
    if (error) {
      libc_errno = static_cast<int>(-error);
      return -1;
    }
    error = mmix::retain_shared_signal(
        handler, reinterpret_cast<uintptr_t>(prepared.sa_restorer), &lease);
    if (error) {
      if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask,
                             nullptr, sizeof(saved_mask)))
        __builtin_trap();
      libc_errno = static_cast<int>(error);
      return -1;
    }
  }
#endif
  auto result = mmix::rt_sigaction(signal, action ? &prepared : nullptr, old_action);
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (retain) {
    // A successful disposition keeps both images resident, even after reset:
    // Linux may already have delivered an older handler/restorer to a peer.
    mmix::finish_shared_signal(&lease, static_cast<bool>(result));
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask, nullptr,
                           sizeof(saved_mask)))
      __builtin_trap();
  }
#endif
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  if (old_action)
    old_action->sa_mask.__signals[0] &= ~(uint64_t(1) << 31);
#endif
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
