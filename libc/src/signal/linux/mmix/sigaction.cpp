//===-- MMIX Linux signal registration ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigaction.h"
#include "private_signals.h"
#include "rt_sigaction.h"
#include "signal_trampoline.h"
#ifdef LIBC_MMIX_SIGNAL_BOUNDARIES
#include "signal_entry.h"
#endif
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif
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
  if (signal <= 0 || signal >= NSIG || mmix::private_signal(signal)) {
    libc_errno = EINVAL;
    return -1;
  }
  struct sigaction prepared{};
  if (action) {
    prepared = *action;
    prepared.sa_mask.__signals[0] &= ~mmix::private_signal_mask;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
    prepared.sa_mask.__signals[0] |= uint64_t(1) << 31;
#endif
    if (!(prepared.sa_flags & SA_RESTORER)) {
      prepared.sa_flags |= SA_RESTORER;
#ifdef LIBC_MMIX_SIGNAL_BOUNDARIES
      prepared.sa_restorer = __llvm_libc_mmix_application_signal_trampoline;
#else
      prepared.sa_restorer = __llvm_libc_mmix_signal_trampoline;
#endif
    }
  }
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_CANCELLATION_WINDOWS)
  uintptr_t handler = prepared.sa_flags & SA_SIGINFO
                          ? reinterpret_cast<uintptr_t>(prepared.sa_sigaction)
                          : reinterpret_cast<uintptr_t>(prepared.sa_handler);
  bool caught = action && handler != reinterpret_cast<uintptr_t>(SIG_DFL) &&
                handler != reinterpret_cast<uintptr_t>(SIG_IGN);
#endif
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  if (caught)
    mmix::revoke_startup_io_fast_path();
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
  loader::LoaderSignalLease lease;
  uint64_t saved_mask = 0;
  if (caught) {
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
  if (caught) {
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
  if (old_action)
    old_action->sa_mask.__signals[0] &= ~mmix::private_signal_mask;
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
