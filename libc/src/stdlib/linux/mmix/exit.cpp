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
#include "src/__support/threads/thread.h"
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
enum class NormalExitPhase {
  None,
  ThreadCallbacks,
  ProcessCallbacks,
  LastThreadProcessCallbacks
};
static LIBC_CONSTINIT LIBC_THREAD_LOCAL NormalExitPhase normal_exit_phase =
    NormalExitPhase::None;
static LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadAttributes *worker_cleanup =
    nullptr;
static LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadAttributes *worker_draining =
    nullptr;
static mmix::LifecycleWord process_exit_wait;
static LIBC_CONSTINIT LIBC_THREAD_LOCAL mmix::UnwindRoot last_thread_root;
static LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadAttributes *last_thread_attributes =
    nullptr;

static bool last_thread_phase_matches() {
  auto *control = mmix::current_control;
  return normal_exit_phase == NormalExitPhase::LastThreadProcessCallbacks &&
         control && self.attrib == &control->attributes &&
         last_thread_attributes == self.attrib &&
         main_thread_cleanup_finished() &&
         control->terminal_cleanup_finished() &&
         control->unwind_root.kind == mmix::UnwindRootKind::LastThreadProcess &&
         last_thread_root.owner == control &&
         last_thread_root.matches(control->unwind_root);
}

static bool normal_exit_phase_matches() {
  auto *control = mmix::current_control;
  if (control && !control->retains_resources_until_process_exit())
    return self.attrib == &control->attributes &&
           ((normal_exit_phase == NormalExitPhase::ThreadCallbacks &&
             worker_draining == self.attrib) ||
            (normal_exit_phase == NormalExitPhase::ProcessCallbacks &&
             worker_cleanup == self.attrib));
  return (normal_exit_phase == NormalExitPhase::ThreadCallbacks &&
          main_thread_cleanup_in_progress()) ||
         (normal_exit_phase == NormalExitPhase::ProcessCallbacks &&
          main_thread_cleanup_finished());
}
#endif

pid_t normal_exit_fork_owner(bool *during_cleanup) {
  if (during_cleanup)
    *during_cleanup = false;
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (normal_exit_phase_matches() && control && control->attributes.tid > 0 &&
      !control->cancellation.terminal() &&
      process_cleanup_tid == uint64_t(control->attributes.tid)) {
    if (during_cleanup)
      *during_cleanup = normal_exit_phase == NormalExitPhase::ThreadCallbacks;
    return control->attributes.tid;
  }
#endif
  return 0;
}

bool rebind_normal_exit_fork_owner([[maybe_unused]] pid_t previous_tid) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!normal_exit_phase_matches() || !control || previous_tid <= 0 ||
      control->attributes.tid <= 0 || control->attributes.tid == previous_tid ||
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

pid_t last_thread_exit_fork_owner() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (last_thread_phase_matches() && control->attributes.tid > 0 &&
      process_cleanup_tid == uint64_t(control->attributes.tid))
    return control->attributes.tid;
#endif
  return 0;
}

bool rebind_last_thread_exit_fork_owner([[maybe_unused]] pid_t previous_tid) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!last_thread_phase_matches() || previous_tid <= 0 ||
      control->attributes.tid <= 0 || control->attributes.tid == previous_tid ||
      process_cleanup_tid != uint64_t(previous_tid) ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork())
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

[[noreturn]] static void finish_process_exit(int status) {
  __cxa_finalize(nullptr);
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  normal_exit_phase = NormalExitPhase::None;
  worker_cleanup = nullptr;
  worker_draining = nullptr;
  last_thread_attributes = nullptr;
#endif
#ifdef LIBC_MMIX_CONCURRENT_STDIO
  fflush(nullptr);
#endif
  internal::exit(status);
}

[[noreturn]] void exit_after_thread_cleanup() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->terminal_cleanup_finished() || !main_thread_cleanup_finished() ||
      !control->unwind_root.active || control->unwind_root.owner != control ||
      control->unwind_root.kind != mmix::UnwindRootKind::LastThreadProcess ||
      !control->unwind_root.generation || !control->unwind_root.chain)
    __builtin_trap();
  begin_process_exit(0);
  last_thread_root = control->unwind_root;
  last_thread_attributes = self.attrib;
  normal_exit_phase = NormalExitPhase::LastThreadProcessCallbacks;
  finish_process_exit(0);
#else
  __builtin_trap();
#endif
}

[[noreturn]] void exit_process(int status) {
  begin_process_exit(status);
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  // Only normal process exit supplies this provenance. The query also checks
  // matching cleanup completion/phase; fork must independently prove its root.
  normal_exit_phase = NormalExitPhase::ThreadCallbacks;
  worker_cleanup = nullptr;
  worker_draining = nullptr;
  auto *control = mmix::current_control;
  if (control && !control->retains_resources_until_process_exit() &&
      self.attrib == &control->attributes)
    worker_draining = self.attrib;
#endif
  cleanup_current_thread();
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  worker_draining = nullptr;
  // Workers never consult main's mutable phase. Bind completion only after
  // their own TLS/TSS drain returned, not merely when callbacks_started rose.
  control = mmix::current_control;
  if (control && !control->retains_resources_until_process_exit() &&
      self.attrib == &control->attributes)
    worker_cleanup = self.attrib;
  normal_exit_phase = NormalExitPhase::ProcessCallbacks;
#endif
  finish_process_exit(status);
}
} // namespace internal

[[noreturn]] LLVM_LIBC_FUNCTION(void, exit, (int status)) {
  internal::exit_process(status);
}

} // namespace LIBC_NAMESPACE_DECL
