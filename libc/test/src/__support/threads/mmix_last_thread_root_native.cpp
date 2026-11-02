//===-- Native MMIX last-thread root invariants ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Inject only cursor/kernel boundaries. This tests production root admission
// and fresh-generation capture, not a real MMIX forced-unwind continuation.
#include <cassert>
#include <csignal>
#include <linux/futex.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/unwind_root.cpp"

namespace M = LIBC_NAMESPACE::mmix;
static unsigned window_initializations;
static bool cleanup_pending, dispatch_pending;
static uintptr_t expected_activation;
static uint64_t chain = 41;
struct Completed {};

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control;
bool has_c_cleanup() { return cleanup_pending; }
bool validate_fork_cleanup(const UnwindRoot &) { return !dispatch_pending; }
// Active stack-cleanup witnesses are outside this terminal-root fixture.
bool snapshot_active_c_cleanup(const UnwindRoot &, ActiveCleanupWitness &) {
  __builtin_trap();
}
bool validate_active_c_cleanup(const UnwindRoot &,
                              const ActiveCleanupWitness &) {
  __builtin_trap();
}
bool snapshot_pending_cleanup(const UnwindRoot &, PendingCleanupWitness &) {
  __builtin_trap();
}
bool validate_pending_cleanup(const UnwindRoot &, const PendingCleanupWitness &) {
  __builtin_trap();
}
void dispatch_c_cleanup(uintptr_t, uintptr_t, uintptr_t) { __builtin_trap(); }
void initialize_cancellation_windows(ThreadControl &) {
  ++window_initializations;
}
void begin_thread_cleanup(ThreadControl &) { __builtin_trap(); }
[[noreturn]] void complete_thread_cleanup(ThreadControl &, ThreadReturnValue) {
  __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" int unw_getcontext(unw_context_t *) { return 0; }
extern "C" int unw_init_local(unw_cursor_t *, unw_context_t *) { return 0; }
extern "C" int unw_step(unw_cursor_t *) { return 1; }
extern "C" int unw_is_signal_frame(unw_cursor_t *) { return 0; }
extern "C" int unw_get_reg(unw_cursor_t *, int reg, unw_word_t *value) {
  *value = reg == UNW_MMIX_RO ? 0x1000 : 0x2000;
  return 0;
}
extern "C" int unw_get_proc_info(unw_cursor_t *, unw_proc_info_t *info) {
  info->start_ip = expected_activation;
  return 0;
}
extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long first,
                                         long second, long, long, long, long) {
  if (number == __NR_mmix_rstack_query) {
    reinterpret_cast<mmix_rstack_query *>(first)->chain_id = chain;
    return 0;
  }
  assert(number == SYS_futex && second == FUTEX_WAKE_PRIVATE);
  return 0;
}

static void ordinary_body(void *) {
  assert(M::current_control->unwind_root.active);
  assert(M::current_control->unwind_root.kind == M::UnwindRootKind::Thread);
}

static void terminal_body(void *) {
  auto &control = *M::current_control;
  assert(control.unwind_root.generation == 3);
  assert(control.unwind_root.procedure == expected_activation);
  assert(control.unwind_root.kind == M::UnwindRootKind::LastThreadProcess);
  assert(control.cancellation.terminal() && !control.cancellation.ready());
  assert(control.cancellation_identity == 9 &&
         control.cancellation_generation == 7);
  assert(window_initializations == 1);
  assert(M::last_thread_fork_context(control));
  assert(M::restore_fork_unwind(control));
  const auto saved = control.unwind_root;
  M::forced.active = true;
  assert(!M::last_thread_fork_context(control));
  assert(!M::restore_fork_unwind(control));
  M::forced.active = false;
  cleanup_pending = true;
  assert(!M::last_thread_fork_context(control));
  cleanup_pending = false;
  dispatch_pending = true;
  assert(!M::last_thread_fork_context(control));
  dispatch_pending = false;
  ++chain;
  assert(!M::last_thread_fork_context(control));
  --chain;
  control.unwind_root.owner = nullptr;
  assert(!M::last_thread_fork_context(control));
  control.unwind_root = saved;
  control.unwind_root.active = false;
  assert(!M::last_thread_fork_context(control));
  control.unwind_root = saved;
  control.unwind_root.kind = M::UnwindRootKind::Thread;
  assert(!M::last_thread_fork_context(control));
  control.unwind_root = saved;
  assert(M::last_thread_fork_context(control));
  throw Completed{}; // Test transport only; the production body never returns.
}

static void drain_body(void *) {
  auto &control = *M::current_control;
  assert(control.unwind_root.generation == 2);
  assert(control.unwind_root.kind == M::UnwindRootKind::TerminalThreadCleanup);
  assert(control.unwind_root.procedure == expected_activation);
  assert(window_initializations == 1);
  assert(control.cancellation.terminal() && !control.cancellation.ready());
  // Capture precedes actual callback admission.
  assert(!M::terminal_thread_fork_context(control));
  assert(control.begin_callback_cleanup());
  assert(M::terminal_thread_fork_context(control));
  assert(M::restore_fork_unwind(control));
  const auto root = control.unwind_root;
  for (unsigned bad = 0; bad != 7; ++bad) {
    if (bad == 0) M::forced.active = true;
    if (bad == 1) cleanup_pending = true;
    if (bad == 2) dispatch_pending = true;
    if (bad == 3) ++chain;
    if (bad == 4) control.unwind_root.owner = nullptr;
    if (bad == 5) control.unwind_root.active = false;
    if (bad == 6) control.unwind_root.kind = M::UnwindRootKind::Thread;
    assert(!M::terminal_thread_fork_context(control));
    M::forced.active = cleanup_pending = dispatch_pending = false;
    if (bad == 3) --chain;
    control.unwind_root = root;
  }
  assert(M::terminal_thread_fork_context(control));
}

static void worker_terminal_body(void *) {
  auto &control = *M::current_control;
  assert(control.unwind_root.generation == 3);
  assert(control.unwind_root.procedure == expected_activation);
  assert(control.unwind_root.kind == M::UnwindRootKind::LastThreadProcess);
  assert(control.last_worker_cleanup_finished());
  assert(!control.retains_resources_until_process_exit());
  assert(!control.terminal_cleanup_finished() && !control.terminal_cleanup_pending());
  assert(window_initializations == 1 && control.cancellation.terminal());
  assert(control.cancellation_identity == 9 && control.cancellation_generation == 7);
  assert(!M::last_thread_fork_context(control));
  assert(!M::terminal_thread_fork_context(control));
  assert(M::last_worker_fork_context(control) && M::restore_fork_unwind(control));
  const auto root = control.unwind_root;
  for (unsigned bad = 0; bad != 7; ++bad) {
    if (bad == 0) M::forced.active = true;
    if (bad == 1) cleanup_pending = true;
    if (bad == 2) dispatch_pending = true;
    if (bad == 3) ++chain;
    if (bad == 4) control.unwind_root.owner = nullptr;
    if (bad == 5) control.unwind_root.active = false;
    if (bad == 6) control.unwind_root.kind = M::UnwindRootKind::Thread;
    assert(!M::last_worker_fork_context(control));
    M::forced.active = cleanup_pending = dispatch_pending = false;
    if (bad == 3) --chain;
    control.unwind_root = root;
  }
  assert(M::last_worker_fork_context(control));
  throw Completed{};
}

static void worker_drain_body(void *) {
  auto &control = *M::current_control;
  assert(control.worker_cleanup_pending() && !control.worker_cleanup_in_progress());
  assert(control.unwind_root.generation == 2 && window_initializations == 1);
  assert(!M::terminal_worker_fork_context(control));
  assert(control.begin_callback_cleanup());
  assert(M::terminal_worker_fork_context(control) && M::restore_fork_unwind(control));
  assert(!M::terminal_thread_fork_context(control) && !M::last_worker_fork_context(control));
  const auto root = control.unwind_root;
  for (unsigned bad = 0; bad != 9; ++bad) {
    if (bad == 0) M::forced.active = true;
    if (bad == 1) cleanup_pending = true;
    if (bad == 2) dispatch_pending = true;
    if (bad == 3) ++chain;
    if (bad == 4) control.unwind_root.owner = nullptr;
    if (bad == 5) control.unwind_root.active = false;
    if (bad == 6) control.unwind_root.kind = M::UnwindRootKind::Thread;
    if (bad == 7) control.unwind_root.generation = 0;
    if (bad == 8) control.resource_owner.context = &control;
    assert(!M::terminal_worker_fork_context(control));
    M::forced.active = cleanup_pending = dispatch_pending = false;
    if (bad == 3) --chain;
    control.resource_owner = {};
    control.unwind_root = root;
  }
  assert(M::terminal_worker_fork_context(control));
  assert(!control.last_worker_cleanup_finished());
}

static void worker_root() {
  window_initializations = 0;
  M::ThreadRegistry registry;
  M::ThreadControl initial, control;
  initial.attributes.tid = 1;
  control.attributes.tid = 2;
  control.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x3000);
  control.stack_bottom = 0x1800;
  control.stack_top = 0x2800;
  M::current_control = &control;
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.register_main(initial));
    assert(lock.reserve() && lock.insert(control, false) && lock.start(control));
    assert(lock.drop_creator(control));
    assert(lock.begin_cleanup(initial) && lock.close_leases(initial));
    assert(lock.exit_ready(initial, 0));
    initial.clear_tid.value.store(0);
    assert(lock.pin(&initial.attributes) == &initial && lock.claim_join(initial));
    assert(lock.begin_reaping(initial) && lock.unpin(initial) && lock.finish_reaping(initial));
  }
  expected_activation = reinterpret_cast<uintptr_t>(&M::run_with_unwind_root);
  M::run_with_unwind_root(control, ordinary_body, nullptr);
  assert(control.unwind_root.generation == 1 && !control.unwind_root.active);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.begin_cleanup(control));
    assert(!control.record_worker_cleanup_finished());
  }
  expected_activation = reinterpret_cast<uintptr_t>(&M::run_with_terminal_cleanup_root);
  for (unsigned bad = 0; bad != 12; ++bad) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      rlimit limit{0, 0};
      assert(::setrlimit(RLIMIT_CORE, &limit) == 0);
      ::alarm(5);
      if (bad == 0) M::forced.active = true;
      if (bad == 1) control.unwind_root.active = true;
      if (bad == 2) cleanup_pending = true;
      if (bad == 3) dispatch_pending = true;
      if (bad == 4) control.unwind_root.generation = UINT64_MAX;
      if (bad == 5) control.cancellation_transfer.store(1);
      if (bad == 6) control.cancellation_point.store(
          reinterpret_cast<M::CancellationPoint *>(8));
      if (bad == 7) control.stack_bottom = 0x2008;
      if (bad == 8) control.stack_top = 0x2000;
      if (bad == 9) control.cancellation_suppression.store(1);
      if (bad == 10) control.resource_owner.context = &control;
      if (bad == 11) M::current_control = nullptr;
      M::run_with_terminal_cleanup_root(control, worker_drain_body, nullptr);
      ::_exit(1);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    assert(control.unwind_root.generation == 1 && !control.unwind_root.active);
  }
  M::run_with_terminal_cleanup_root(control, worker_drain_body, nullptr);
  assert(control.unwind_root.generation == 2 && !control.unwind_root.active);
  assert(!M::terminal_worker_fork_context(control));
  assert(control.record_worker_cleanup_finished());
  assert(!control.worker_cleanup_in_progress());
  assert(!control.record_worker_cleanup_finished());
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.close_leases(control) && lock.exit_ready(control, 17));
    assert(!control.last_worker_cleanup_finished());
    assert(lock.termination_action(control) == M::ThreadTermination::Process);
  }
  control.cancellation_identity = 9;
  control.cancellation_generation = 7;
  expected_activation = reinterpret_cast<uintptr_t>(&M::run_with_process_exit_root);
  for (unsigned bad = 0; bad != 13; ++bad) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      rlimit limit{0, 0};
      assert(::setrlimit(RLIMIT_CORE, &limit) == 0);
      ::alarm(5);
      if (bad == 0) M::forced.active = true;
      if (bad == 1) control.unwind_root.active = true;
      if (bad == 2) cleanup_pending = true;
      if (bad == 3) dispatch_pending = true;
      if (bad == 4) control.unwind_root.generation = UINT64_MAX;
      if (bad == 5) control.cancellation_transfer.store(1);
      if (bad == 6) control.cancellation_point.store(
          reinterpret_cast<M::CancellationPoint *>(8));
      if (bad == 7) control.stack_bottom = 0x2008;
      if (bad == 8) control.stack_top = 0x2000;
      if (bad == 9) control.attributes.atexit_callback_mgr = nullptr;
      if (bad == 10) control.resource_owner.context = &control;
      if (bad == 11) control.cancellation.restore_after_fork();
      if (bad == 12) M::current_control = nullptr;
      M::run_with_process_exit_root(control, worker_terminal_body, nullptr);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    assert(control.unwind_root.generation == 2 && !control.unwind_root.active);
  }
  try {
    M::run_with_process_exit_root(control, worker_terminal_body, nullptr);
  } catch (Completed &) {
    assert(control.last_worker_cleanup_finished());
    assert(!control.retains_resources_until_process_exit());
    assert(control.unwind_root.generation == 3 && window_initializations == 1);
    return;
  }
  __builtin_trap();
}

int main() {
  M::ThreadRegistry registry;
  M::ThreadControl control;
  control.attributes.tid = ::getpid();
  M::current_control = &control;
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.register_main(control));
  }
  expected_activation = reinterpret_cast<uintptr_t>(&M::run_with_unwind_root);
  M::run_with_unwind_root(control, ordinary_body, nullptr);
  assert(!control.unwind_root.active && control.unwind_root.generation == 1);
  assert(window_initializations == 1);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.begin_cleanup(control));
    assert(!control.terminal_cleanup_finished());
  }
  expected_activation =
      reinterpret_cast<uintptr_t>(&M::run_with_terminal_cleanup_root);
  for (unsigned bad = 0; bad != 9; ++bad) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      rlimit limit{0, 0};
      assert(::setrlimit(RLIMIT_CORE, &limit) == 0);
      ::alarm(5);
      if (bad == 0) M::forced.active = true;
      if (bad == 1) control.unwind_root.active = true;
      if (bad == 2) cleanup_pending = true;
      if (bad == 3) dispatch_pending = true;
      if (bad == 4) control.unwind_root.generation = UINT64_MAX;
      if (bad == 5) control.cancellation_transfer.store(1);
      if (bad == 6) control.cancellation_point.store(
          reinterpret_cast<M::CancellationPoint *>(8));
      if (bad == 7) control.cancellation_suppression.store(1);
      if (bad == 8) M::current_control = nullptr;
      M::run_with_terminal_cleanup_root(control, drain_body, nullptr);
      ::_exit(1);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    assert(control.unwind_root.generation == 1 && !control.unwind_root.active);
  }
  M::run_with_terminal_cleanup_root(control, drain_body, nullptr);
  assert(!control.unwind_root.active && control.unwind_root.generation == 2);
  assert(window_initializations == 1);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.close_leases(control) && lock.exit_ready(control, 42));
    assert(lock.termination_action(control) == M::ThreadTermination::Process);
  }
  control.cancellation_identity = 9;
  control.cancellation_generation = 7;
  expected_activation =
      reinterpret_cast<uintptr_t>(&M::run_with_process_exit_root);
  // None of these may overwrite a consumed/live root or escape to the body.
  for (unsigned bad = 0; bad != 6; ++bad) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      rlimit limit{0, 0};
      assert(::setrlimit(RLIMIT_CORE, &limit) == 0);
      ::alarm(5);
      if (bad == 0)
        M::forced.active = true;
      if (bad == 1)
        control.unwind_root.active = true;
      if (bad == 2)
        cleanup_pending = true;
      if (bad == 3)
        dispatch_pending = true;
      if (bad == 4)
        control.unwind_root.generation = UINT64_MAX;
      if (bad == 5)
        control.cancellation_transfer.store(1);
      M::run_with_process_exit_root(control, terminal_body, nullptr);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    assert(control.unwind_root.generation == 2 && !control.unwind_root.active);
    assert(!M::forced.active && !cleanup_pending && !dispatch_pending);
  }
  try {
    M::run_with_process_exit_root(control, terminal_body, nullptr);
  } catch (Completed &) {
    assert(control.terminal_cleanup_finished());
    assert(control.unwind_root.generation == 3 && window_initializations == 1);
    worker_root();
    return 0;
  }
  __builtin_trap();
}
