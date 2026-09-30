//===-- MMIX Linux final user-thread completion ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_finish.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "main_thread.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/thread.h"
#include "thread_start.h"
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM ||                           \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux thread completion requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

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
  set_mask(UINT64_MAX);
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
  __llvm_libc_mmix_thread_exit();
}

extern "C" [[clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_run(ThreadControl *control) {
  if (!control || internal::self.attrib != &control->attributes ||
      control->retains_resources_until_process_exit() ||
      control->acquire_child() != ChildStatus::Ready ||
      control->acquire_decision() != CreatorDecision::Go)
    __builtin_trap();
  set_mask(control->saved_signal_mask);
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

[[noreturn]] void exit_thread(ThreadControl &control,
                              ThreadReturnValue result) {
  if (internal::self.attrib != &control.attributes)
    __builtin_trap();
  uint64_t callback_mask;
  set_mask(UINT64_MAX, &callback_mask);
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.begin_cleanup(control))
      __builtin_trap();
  }
  set_mask(callback_mask);
  // Reuse only the selected internal callback/TSS manager. Full public TLS
  // destruction and cancellation need their own integration.
  if (control.retains_resources_until_process_exit())
    internal::cleanup_main_thread();
  else
    internal::call_atexit_callbacks(&control.attributes);
  finish_thread(control, result);
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
