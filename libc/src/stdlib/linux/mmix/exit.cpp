//===-- MMIX Linux normal termination -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/exit.h"
#include "process_exit.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/main_thread.h"
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/linux/mmix/credentials.h"
#include <sys/syscall.h>
#endif
#ifdef LIBC_MMIX_CONCURRENT_STDIO
#include "src/stdio/fflush.h"
#endif

namespace LIBC_NAMESPACE_DECL {

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
namespace mmix {
void close_notification_admission() __attribute__((weak));
}
#endif

extern "C" void __cxa_finalize(void *);

namespace internal {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
static LIBC_CONSTINIT LIBC_THREAD_LOCAL uint64_t process_cleanup_tid = 0;
static LIBC_CONSTINIT LIBC_THREAD_LOCAL bool normal_process_callbacks = false;
static mmix::LifecycleWord process_exit_wait;
#endif

pid_t normal_exit_fork_owner() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (normal_process_callbacks && main_thread_cleanup_finished() && control &&
      control->attributes.tid > 0 && !control->cancellation.terminal() &&
      process_cleanup_tid == uint64_t(control->attributes.tid))
    return control->attributes.tid;
#endif
  return 0;
}

bool rebind_normal_exit_fork_owner([[maybe_unused]] pid_t previous_tid) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!normal_process_callbacks || !main_thread_cleanup_finished() ||
      !control || previous_tid <= 0 || control->attributes.tid <= 0 ||
      control->attributes.tid == previous_tid ||
      process_cleanup_tid != uint64_t(previous_tid) ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork() || control->cancellation.terminal())
    return false;
  process_cleanup_tid = uint64_t(control->attributes.tid);
  return true;
#else
  return false;
#endif
}

void begin_process_exit(int status, [[maybe_unused]] bool quick) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  // Losing exit callers remain kernel tasks until exit_group. Keep them
  // responsive if the cleanup owner changes credentials from a callback.
  uint64_t blocked = UINT64_MAX & ~mmix::CREDENTIAL_MASK, previous;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&blocked),
                   reinterpret_cast<long>(&previous), sizeof(blocked)) != 0)
    __builtin_trap();
  // Recursive exit from a destructor or process callback must not replay it
  // or deadlock on the callback list. Immediate termination skips the rest.
  auto *control = mmix::current_control;
  // The fork transport publishes the new TID before child handlers run. Do not
  // mistake the parent's inherited TLS ownership marker for child recursion.
  // Coordinated recovery of this owner's normal callback continuation instead
  // explicitly rebinds the marker before child callbacks run.
  uint64_t tid = control ? uint64_t(control->attributes.tid) : UINT64_MAX;
  if (process_cleanup_tid == tid)
    internal::exit(status);
  if (quick && control) {
    control->cancellation.set_enabled(false);
    if (control->cancellation_suppression.fetch_add(1) == UINT64_MAX)
      __builtin_trap();
  }
  // _Fork intentionally preserves interrupted locks and may copy a peer's
  // cleanup claim. Only its exec-only, single-task child can override that
  // claim. Ordinary recoverable children reset it during registry restoration;
  // a certified normal-exit continuation must preserve its own cleanup claim.
  bool sole_fork_child =
      control && !control->fork_recovery.allows_thread_creation();
  if (mmix::close_notification_admission)
    mmix::close_notification_admission();
  bool owner = mmix::thread_registry.claim_process_cleanup(sole_fork_child);
  if (!owner) {
    // The winner never releases ownership: exit_group terminates every peer.
    // Wait without registry/stream locks and without busy-spinning.
    for (;;) {
      auto waited = process_exit_wait.value.wait(0);
      if (!waited && waited.error() != EINTR && waited.error() != EAGAIN)
        __builtin_trap();
    }
  }
  process_cleanup_tid = tid;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&previous), 0, sizeof(previous)) != 0)
    __builtin_trap();
#endif
}

[[noreturn]] void exit_process(int status) {
  begin_process_exit(status);
  cleanup_current_thread();
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  // A recursive exit from TLS/TSS cleanup has not completed that drain. The
  // query additionally checks the Finished phase and the ordinary fork caller
  // must still prove its active root; this marker alone grants no admission.
  normal_process_callbacks = true;
#endif
  __cxa_finalize(nullptr);
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  normal_process_callbacks = false;
#endif
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
