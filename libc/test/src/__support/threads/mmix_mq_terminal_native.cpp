//===-- Native MMIX retained MQ terminal census ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Real registry/bootstrap paths; helper creation/reclamation and clone are
// injected. Not guest MQ delivery, kernel retirement or signal evidence.
#include <cassert>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/thread_reaper.cpp"

namespace M = LIBC_NAMESPACE::mmix;
namespace I = LIBC_NAMESPACE::internal;
static M::ThreadControl initial, callback, helper, extra;
static unsigned reclaimed;
static void *runner(void *) { __builtin_trap(); }
static long owner_hook(void *) { __builtin_trap(); }
static M::ThreadResourceOwner owner() {
  return {&callback, owner_hook, owner_hook, runner};
}

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
ThreadCreationResult create_thread_helper(const ThreadPreparation &,
                                         ThreadAttributes *&attributes,
                                         ThreadControl **output) {
  ThreadRegistryLock lock(thread_registry);
  assert(lock.begin_helper_creation());
  assert(lock.insert(helper, true, true) && lock.start(helper));
  assert(lock.drop_creator(helper));
  helper.attributes.platform_data = &helper.clear_tid.value;
  attributes = &helper.attributes;
  *output = &helper;
  return {};
}
long reclaim_thread(ThreadControl &control, ThreadOwner expected,
                    ThreadResources &) {
  assert(&control == &helper && expected == ThreadOwner::Detached);
  assert(reaper_stop_requested() && !reclaimed++);
  ThreadRegistryLock lock(thread_registry);
  assert(lock.begin_cleanup(control) && lock.close_leases(control));
  assert(lock.exit_ready(control, 0));
  assert(!lock.begin_reaping(control)); // No terminal-clear observation yet.
  control.clear_tid.value.store(0);
  assert(lock.begin_reaping(control) && lock.finish_reaping(control));
  return 0;
}
extern "C" void __llvm_libc_mmix_reaper_loop() { __builtin_trap(); }
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long, long op,
                                         long, long, long, long) {
  assert(number == SYS_futex && op == FUTEX_WAKE_PRIVATE);
  return 0;
}

static void check_phase() {
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(lock.callback_phase(callback) == M::CallbackPhase::Active);
  assert(lock.owner(callback) == M::ThreadOwner::Detached);
  assert(callback.retained_active_callback());
}

static void terminal_copies(M::ForkContinuation kind) {
  auto root = callback.unwind_root;
  auto *manager = callback.attributes.atexit_callback_mgr;
  const bool was_child = callback.fork_recovery.is_child();
  const auto tid = callback.attributes.tid;
  bool single = true;
  assert(!M::thread_registry.freeze_for_fork(callback, 1, &single, false, kind));
  assert(!single);
  assert(M::reaper_absent_for_exit_fork(callback, kind));
  callback.resource_owner = {};
  assert(!M::thread_registry.freeze_for_fork(callback, 1, &single, true, kind));
  // AIO uses a different owner without a terminal-runner certificate.
  callback.resource_owner = {&extra, owner_hook, owner_hook};
  assert(!M::thread_registry.freeze_for_fork(callback, 1, &single, true, kind));
  callback.resource_owner = owner();
  assert(M::thread_registry.freeze_for_fork(callback, 1, &single, true, kind));
  assert(single);
  M::thread_registry.release_after_fork(); // Injected clone failure.
  assert(callback.fork_recovery.is_child() == was_child);
  assert(callback.attributes.tid == tid);
  assert(root.matches(callback.unwind_root));
  assert(manager == callback.attributes.atexit_callback_mgr);
  check_phase();
  for (unsigned i = 0; i != 2; ++i) {
    assert(M::thread_registry.freeze_for_fork(callback, 1, &single, true, kind));
    assert(single); // Requires the retained Active contribution to remain 1.
    M::thread_registry.restore_fork_child(
        callback, I::ForkRecovery::child(true, true), kind);
    assert(root.matches(callback.unwind_root));
    assert(manager == callback.attributes.atexit_callback_mgr);
    check_phase();
  }
}

static void extra_terminal_census(unsigned mode) {
  M::ThreadRegistry registry;
  M::ThreadControl main, caller, waiter;
  main.attributes.tid = 1;
  caller.attributes.style = LIBC_NAMESPACE::ThreadStyle::POSIX;
  caller.runner.posix_runner = runner;
  caller.argument = &caller;
  caller.resource_owner = {&caller, owner_hook, owner_hook, runner};
  caller.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.register_main(main));
    assert(lock.reserve() && lock.insert_callback_waiter(caller));
    assert(lock.start(caller) && lock.activate_callback(caller));
    assert(lock.drop_creator(caller));
    assert(lock.reserve() && lock.insert_callback_waiter(waiter));
    assert(lock.start(waiter));
    if (mode == 2) assert(lock.discard_callback(waiter));
    if (mode == 3) assert(lock.activate_callback(waiter));
    if (mode != 1) {
      assert(lock.drop_creator(waiter) && lock.begin_cleanup(waiter));
      assert(lock.close_leases(waiter) && lock.exit_ready(waiter, 0));
      waiter.clear_tid.value.store(0);
      assert(lock.begin_reaping(waiter));
    }
    assert(lock.begin_cleanup(main) && lock.close_leases(main));
    assert(lock.exit_ready(main, 0));
    main.clear_tid.value.store(0);
    assert(lock.pin(&main.attributes) && lock.claim_join(main));
    assert(lock.begin_reaping(main) && lock.unpin(main));
    assert(lock.finish_reaping(main));
    assert(lock.begin_cleanup(caller) && caller.begin_callback_cleanup());
    assert(lock.live_threads() == 1 && !lock.queued_reclaims());
  }
  caller.cancellation_suppression.store(1);
  caller.unwind_root = {&caller, 8, 16, 24, 4, 2, true,
                       M::UnwindRootKind::TerminalThreadCleanup};
  assert(caller.worker_cleanup_in_progress());
  bool single = true;
  assert(!registry.freeze_for_fork(caller, 1, &single, true,
                                  M::ForkContinuation::WorkerThreadDuring));
  assert(!single);
  // Record completion does not consume either unreaped callback contribution.
  assert(caller.record_worker_cleanup_finished());
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.close_leases(caller) && lock.exit_ready(caller, 0));
    assert(lock.termination_action(caller) == M::ThreadTermination::Process);
    assert(lock.claim_process_cleanup());
    assert(lock.queued_reclaims() == 1);
  }
  caller.unwind_root.kind = M::UnwindRootKind::LastThreadProcess;
  assert(caller.last_worker_cleanup_finished());
  assert(!registry.freeze_for_fork(caller, 1, &single, true,
                                  M::ForkContinuation::LastWorkerAfter));
  {
    M::ThreadRegistryLock lock(registry);
    if (mode == 1) {
      assert(lock.discard_callback(waiter) && lock.drop_creator(waiter));
      assert(lock.begin_cleanup(waiter) && lock.close_leases(waiter));
      assert(lock.exit_ready(waiter, 0));
      waiter.clear_tid.value.store(0);
      assert(lock.begin_reaping(waiter));
    }
    assert(lock.finish_reaping(waiter));
    assert(lock.callback_phase(caller) == M::CallbackPhase::Active);
  }
  assert(registry.freeze_for_fork(caller, 1, &single, true,
                                 M::ForkContinuation::LastWorkerAfter));
  assert(single);
  registry.release_after_fork();
}

static void run(unsigned mode) {
  if (mode) extra_terminal_census(mode);
  callback.attributes.style = LIBC_NAMESPACE::ThreadStyle::POSIX;
  callback.runner.posix_runner = runner;
  callback.argument = &callback;
  callback.resource_owner = owner();
  callback.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
  initial.attributes.tid = 1;
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.register_main(initial));
    assert(lock.reserve() && lock.insert_callback_waiter(callback));
    assert(!callback.retained_active_callback());
    assert(lock.start(callback) && lock.activate_callback(callback));
    assert(lock.drop_creator(callback));
    callback.cancellation.set_enabled(true);
    assert(lock.begin_cleanup(initial) && lock.close_leases(initial));
    assert(lock.exit_ready(initial, 0));
    initial.clear_tid.value.store(0);
    assert(lock.pin(&initial.attributes) && lock.claim_join(initial));
    assert(lock.begin_reaping(initial) && lock.unpin(initial));
    assert(lock.finish_reaping(initial));
  }
  assert(!M::ensure_thread_reaper({}, 8192).error);
  if (mode) {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.reserve() && lock.insert_callback_waiter(extra));
    assert(lock.start(extra));
    if (mode == 2) assert(lock.discard_callback(extra));
    if (mode == 3) assert(lock.activate_callback(extra));
    if (mode != 1) assert(lock.drop_creator(extra));
    assert(!lock.sole_fork_caller(callback));
  }
  if (mode) {
    assert(!M::retire_reaper_for_fork(callback) && !reclaimed);
    M::ThreadRegistryLock lock(M::thread_registry);
    if (mode == 1) {
      assert(lock.discard_callback(extra) && lock.drop_creator(extra));
    }
    assert(lock.begin_cleanup(extra) && lock.close_leases(extra));
    assert(lock.exit_ready(extra, 0));
    extra.clear_tid.value.store(0);
    assert(lock.begin_reaping(extra));
    // Unlisted, no queued entry, but its unreaped contribution still exists.
    assert(!lock.sole_fork_caller(callback));
    assert(lock.finish_reaping(extra));
  }
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.sole_fork_caller(callback));
    assert(lock.begin_helper_creation());
    assert(!lock.sole_fork_caller(callback));
    assert(lock.cancel_helper_reservation());
    assert(lock.reserve());
    assert(!lock.sole_fork_caller(callback));
    assert(lock.cancel_reservation());
    callback.resource_owner = {&extra, owner_hook, owner_hook};
    assert(!lock.sole_fork_caller(callback));
    callback.resource_owner = {};
    assert(!lock.sole_fork_caller(callback));
    callback.resource_owner = owner();
    callback.argument = nullptr;
    assert(!lock.sole_fork_caller(callback));
    callback.argument = &callback;
    callback.runner.posix_runner = nullptr;
    assert(!lock.sole_fork_caller(callback));
    callback.runner.posix_runner = runner;
    assert(lock.pin(&callback.attributes) == &callback);
    assert(!lock.sole_fork_caller(callback));
    assert(lock.acquire_lease(callback) && lock.unpin(callback));
    assert(!lock.sole_fork_caller(callback));
    assert(lock.release_lease(callback) && lock.sole_fork_caller(callback));
  }
  assert(M::retire_reaper_for_fork(callback) && reclaimed == 1);
  assert(!M::reaper_stop_requested());
  callback.cancellation_suppression.store(1);
  bool single = true;
  assert(M::thread_registry.freeze_for_fork(callback, 1, &single, true));
  assert(!single); // Retirement does not grant ordinary full child recovery.
  assert(!I::ForkRecovery::child(single, single).allows_fork());
  M::thread_registry.release_after_fork();
  assert(!M::thread_registry.quiescent_fork_caller(callback));
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.begin_cleanup(callback) && callback.begin_callback_cleanup());
  }
  callback.unwind_root = {&callback, 8, 16, 24, 4, 2, true,
                         M::UnwindRootKind::TerminalThreadCleanup};
  assert(callback.worker_cleanup_in_progress());
  terminal_copies(M::ForkContinuation::WorkerThreadDuring);
  assert(callback.record_worker_cleanup_finished());
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.close_leases(callback) && lock.exit_ready(callback, 17));
    assert(lock.termination_action(callback) == M::ThreadTermination::Process);
    assert(lock.claim_process_cleanup());
  }
  callback.unwind_root.kind = M::UnwindRootKind::LastThreadProcess;
  terminal_copies(M::ForkContinuation::LastWorkerAfter);
  assert(callback.last_worker_cleanup_finished());
  assert(reclaimed == 1);
}

int main() {
  for (unsigned mode = 0; mode != 4; ++mode) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      run(mode);
      ::_exit(0);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
}
