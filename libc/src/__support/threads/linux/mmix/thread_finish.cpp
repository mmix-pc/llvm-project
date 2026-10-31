//===-- MMIX Linux final user-thread completion ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_finish.h"
#include "credentials.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "main_thread.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/thread.h"
#include "thread_start.h"
#include <sys/syscall.h>
#ifdef LIBC_MMIX_PUBLIC_ONCE
#include "once.h"
#endif

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM ||                           \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux thread completion requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

#ifdef LIBC_MMIX_UNWIND_ROOTS
namespace {
struct TerminalDrain {
  ThreadControl *control;
  ThreadAttributes *attributes;
  ThreadAtExitCallbackMgr *manager;
  UnwindRoot root;
  pid_t tid;
  bool worker;
};
LIBC_CONSTINIT LIBC_THREAD_LOCAL TerminalDrain *terminal_drain = nullptr;

bool matches_terminal_drain(bool worker) {
  auto *drain = terminal_drain;
  auto *control = current_control;
  return drain && drain->worker == worker && control == drain->control &&
         internal::self.attrib == drain->attributes &&
         drain->attributes == &control->attributes && drain->manager &&
         drain->manager == control->attributes.atexit_callback_mgr &&
         (worker ? control->worker_cleanup_in_progress()
                 : control->terminal_cleanup_in_progress() &&
                       internal::main_thread_cleanup_in_progress()) &&
         drain->root.matches(control->unwind_root);
}

[[gnu::noinline, clang::disable_tail_calls]] void drain_terminal_thread(void *p) {
  auto &control = *static_cast<ThreadControl *>(p);
  if (terminal_drain || internal::self.attrib != &control.attributes ||
      !control.attributes.atexit_callback_mgr ||
      (!control.terminal_cleanup_pending() && !control.worker_cleanup_pending()))
    __builtin_trap();
  TerminalDrain drain{&control, &control.attributes,
                      control.attributes.atexit_callback_mgr,
                      control.unwind_root, control.attributes.tid,
                      control.worker_cleanup_pending()};
  terminal_drain = &drain;
  internal::cleanup_current_thread();
  if (terminal_drain != &drain || current_control != &control ||
      internal::self.attrib != drain.attributes ||
      control.attributes.atexit_callback_mgr != drain.manager ||
      control.attributes.tid != drain.tid ||
      !(drain.worker ? control.worker_cleanup_in_progress()
                     : internal::main_thread_cleanup_finished()) ||
      !drain.root.matches(control.unwind_root))
    __builtin_trap();
  terminal_drain = nullptr;
}
} // namespace

pid_t initial_thread_during_fork_owner() {
  return matches_terminal_drain(false) &&
                 terminal_drain->tid > 0 &&
                 terminal_drain->tid == current_control->attributes.tid
             ? terminal_drain->tid
             : 0;
}

bool rebind_initial_thread_during_fork_owner(pid_t previous_tid) {
  if (!matches_terminal_drain(false) || previous_tid <= 0 ||
      terminal_drain->tid != previous_tid ||
      current_control->attributes.tid <= 0 ||
      current_control->attributes.tid == previous_tid ||
      !current_control->fork_recovery.is_child() ||
      !current_control->fork_recovery.allows_fork())
    return false;
  terminal_drain->tid = current_control->attributes.tid;
  return true;
}

pid_t worker_thread_during_fork_owner() {
  return matches_terminal_drain(true) && terminal_drain->tid > 0 &&
                 terminal_drain->tid == current_control->attributes.tid
             ? terminal_drain->tid
             : 0;
}

bool rebind_worker_thread_during_fork_owner(pid_t previous_tid) {
  if (!matches_terminal_drain(true) || previous_tid <= 0 ||
      terminal_drain->tid != previous_tid ||
      current_control->attributes.tid <= 0 ||
      current_control->attributes.tid == previous_tid ||
      !current_control->fork_recovery.is_child() ||
      !current_control->fork_recovery.allows_fork())
    return false;
  terminal_drain->tid = current_control->attributes.tid;
  return true;
}
#endif

static void set_mask(uint64_t mask, uint64_t *previous = nullptr) {
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&mask),
                   reinterpret_cast<long>(previous), sizeof(mask)) != 0)
    __builtin_trap();
}

static Futex::Timeout lease_deadline() {
  timespec ts;
  if (syscall_impl(SYS_clock_gettime, CLOCK_MONOTONIC,
                   reinterpret_cast<long>(&ts)) != 0 ||
      ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000 ||
      ts.tv_sec == cpp::numeric_limits<time_t>::max())
    __builtin_trap();
  ++ts.tv_sec;
  return *Futex::Timeout::from_timespec(ts, false);
}

[[noreturn]] void finish_thread(ThreadControl &control,
                                ThreadReturnValue result) {
  // Cancellation must remain suppressed through terminal entry. No public
  // cancellation/forced-unwind admission is implied by this internal boundary.
  if (internal::self.attrib != &control.attributes)
    __builtin_trap();
  // Terminal waits still participate in process credential changes.
  set_mask(UINT64_MAX & ~CREDENTIAL_MASK);
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.close_leases(control))
      __builtin_trap();
  }
  for (;;) {
    uint32_t sequence;
    bool ready;
    {
      ThreadRegistryLock lock(thread_registry);
      ready = lock.leases_drained(control);
      if (ready && !lock.exit_ready(control, result))
        __builtin_trap();
      sequence = lock.sequence();
    }
    if (ready)
      break;
    auto waited = thread_registry.wait(sequence, lease_deadline());
    if (!waited && waited.error() != ETIMEDOUT)
      __builtin_trap();
  }
  for (;;) {
    ThreadTermination action;
    uint32_t sequence;
    {
      ThreadRegistryLock lock(thread_registry);
      action = lock.termination_action(control);
      sequence = lock.sequence();
    }
    if (action == ThreadTermination::Process)
      terminate_after_thread_cleanup();
    if (action == ThreadTermination::ThreadOnly)
      break;
    auto waited = thread_registry.wait(sequence, lease_deadline());
    if (!waited && waited.error() != ETIMEDOUT)
      __builtin_trap();
  }
  // The registry lock is gone. The native leaf neither reads TLS nor releases
  // live mappings; only kernel clear plus later reference drain permits reuse.
  {
    CredentialGate credentials;
    unregister_credentials(control);
  }
  __llvm_libc_mmix_thread_exit();
}

static void run_worker(void *argument) {
  auto *control = static_cast<ThreadControl *>(argument);
  // Startup still blocks application signals here. Query the registry before
  // restoring their mask so a handler cannot reenter it while we hold the lock.
  for (;;) {
    CallbackPhase phase;
    uint32_t sequence;
    {
      ThreadRegistryLock lock(thread_registry);
      phase = lock.callback_phase(*control);
      sequence = lock.sequence();
    }
    if (phase == CallbackPhase::None)
      break;
    if (phase == CallbackPhase::Discarded)
      exit_thread(*control, control->attributes.style == ThreadStyle::STDC
                                ? ThreadReturnValue(0)
                                : ThreadReturnValue(nullptr));
    if (phase == CallbackPhase::Active) {
      // A fresh waiter is deferred and disabled. Enable it on its own ordinary
      // chain, outside the registry lock, without consuming the creator pin.
      set_mask(UINT64_MAX & ~CREDENTIAL_MASK);
      control->cancellation.set_enabled(true);
      break;
    }
    // Waiting is not an application activation, but remains a participant in
    // process-wide credential changes. Do not open cancellation delivery yet.
    set_mask(UINT64_MAX & ~CREDENTIAL_MASK);
    auto waited = thread_registry.wait(sequence, lease_deadline());
    if (!waited && waited.error() != ETIMEDOUT)
      __builtin_trap();
  }
  uint64_t mask = control->saved_signal_mask;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  if (control->cancellation.enabled())
    mask &= ~(uint64_t(1) << 31);
#endif
  set_mask(mask & ~CREDENTIAL_MASK);
  ThreadReturnValue result;
  if (control->attributes.style == ThreadStyle::POSIX &&
      control->runner.posix_runner)
    result = ThreadReturnValue(control->runner.posix_runner(control->argument));
  else if (control->attributes.style == ThreadStyle::STDC &&
           control->runner.stdc_runner)
    result = ThreadReturnValue(control->runner.stdc_runner(control->argument));
  else
    __builtin_trap();

  exit_thread(*control, result);
}

extern "C" [[clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_run(ThreadControl *control) {
  if (!control || internal::self.attrib != &control->attributes ||
      control->retains_resources_until_process_exit() ||
      control->acquire_child() != ChildStatus::Ready ||
      control->acquire_decision() != CreatorDecision::Go)
    __builtin_trap();
#ifdef LIBC_MMIX_UNWIND_ROOTS
  run_with_unwind_root(*control, run_worker, control);
#else
  run_worker(control);
#endif
}

void begin_thread_cleanup(ThreadControl &control) {
  if (internal::self.attrib != &control.attributes)
    __builtin_trap();
  uint64_t callback_mask;
  set_mask(UINT64_MAX, &callback_mask);
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.begin_cleanup(control))
      __builtin_trap();
  }
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  callback_mask |= uint64_t(1) << 31;
#endif
  set_mask(callback_mask);
}

[[noreturn]] void complete_thread_cleanup(ThreadControl &control,
                                        ThreadReturnValue result) {
  control.unwind_root.active = false;
#ifdef LIBC_MMIX_PUBLIC_ONCE
  uint64_t callback_mask;
  set_mask(UINT64_MAX, &callback_mask);
  abandon_once_initializers();
  set_mask(callback_mask);
#endif
  // Stack cleanup has finished. Drain the selected TLS/TSS manager on its owner.
#ifdef LIBC_MMIX_UNWIND_ROOTS
  if (control.terminal_cleanup_pending() || control.worker_cleanup_pending())
    run_with_terminal_cleanup_root(control, drain_terminal_thread, &control);
  else
#endif
    internal::cleanup_current_thread();
  // Keep this separate from callbacks_started, which is published before the
  // first callback. Initial and managed threads do not gain worker provenance.
  control.record_worker_cleanup_finished();
  finish_thread(control, result);
}

[[noreturn]] void exit_thread(ThreadControl &control,
                              ThreadReturnValue result) {
#ifdef LIBC_MMIX_UNWIND_ROOTS
  if (has_c_cleanup())
    __builtin_trap();
#endif
  begin_thread_cleanup(control);
  complete_thread_cleanup(control, result);
}

extern "C" [[clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_abort(ThreadControl *control) {
  if (!control || control->acquire_decision() != CreatorDecision::Abort)
    __builtin_trap();
  set_mask(UINT64_MAX);
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.abort_ready(*control))
      __builtin_trap();
  }
  // Failed initialization may not have installed self or a callback manager.
  // Never invoke callbacks or restore the application mask on this path.
  __llvm_libc_mmix_thread_exit();
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
