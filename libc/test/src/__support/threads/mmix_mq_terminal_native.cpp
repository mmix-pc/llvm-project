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
  single = true;
  assert(!registry.freeze_for_fork(
      caller, 1, &single, false, M::ForkContinuation::WorkerThreadDuring,
      M::ForkSnapshot::TerminalExecOnly));
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

static void terminal_exec_only_registry(bool initial_owner, bool with_helpers,
                                        bool cancel) {
  M::ThreadRegistry registry;
  M::ThreadControl main, worker, helpers[3], peer;
  auto &caller = initial_owner ? main : worker;
  const auto kind = initial_owner ? M::ForkContinuation::InitialThreadDuring
                                  : M::ForkContinuation::WorkerThreadDuring;
  constexpr auto snapshot = M::ForkSnapshot::TerminalExecOnly;
  main.attributes.tid = 1;
  worker.attributes.tid = 2;
  worker.attributes.style = LIBC_NAMESPACE::ThreadStyle::POSIX;
  worker.runner.posix_runner = runner;
  worker.argument = &worker;
  worker.resource_owner = {&worker, owner_hook, owner_hook, runner};
  worker.control_mapping = {0x2000, 8192};
  worker.stack_mapping = {0x4000, 8192};
  worker.stack_bottom = 0x4000;
  worker.stack_top = 0x6000;
  worker.tls = {8192, 0x6000, 0x7000};
  worker.owns_stack = true;
  caller.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.register_main(main));
    if (!initial_owner) {
      assert(lock.reserve() && lock.insert_callback_waiter(worker));
      assert(lock.start(worker) && lock.activate_callback(worker));
      assert(lock.begin_cleanup(main) && lock.close_leases(main));
      assert(lock.exit_ready(main, 0));
      main.clear_tid.value.store(0);
      assert(lock.pin(&main.attributes) && lock.claim_join(main));
      assert(lock.begin_reaping(main) && lock.unpin(main));
      assert(lock.finish_reaping(main));
    }
    if (with_helpers) {
      for (auto &live_helper : helpers) {
        assert(lock.begin_helper_creation());
        assert(lock.insert(live_helper, true, true) && lock.start(live_helper));
        assert(lock.drop_creator(live_helper));
      }
    }
    caller.cancellation.set_enabled(true);
    caller.cancellation.set_async(true);
    assert(lock.request_cancellation(caller, true) == 0);
    assert(lock.begin_cleanup(caller, cancel));
  }
  caller.cancellation_identity = 9;
  caller.cancellation_generation = 7;
  caller.cancellation_suppression.store(1);
  caller.unwind_root = {&caller, 8, 16, 24, 4, 2, true,
                       M::UnwindRootKind::TerminalThreadCleanup};
  const auto root = caller.unwind_root;
  const auto resource_owner = caller.resource_owner;
  auto *manager = caller.attributes.atexit_callback_mgr;
  auto rejected = [&](M::ForkContinuation selected) {
    bool single = true;
    assert(!registry.freeze_for_fork(caller, 1, &single, false, selected,
                                    snapshot));
    assert(!single);
  };
  rejected(kind); // Stack cleanup alone is not a TLS/TSS drain certificate.
  assert(caller.begin_callback_cleanup());
  if (!initial_owner) {
    rejected(kind); // Activation does not consume the creator's pin.
    M::ThreadRegistryLock lock(registry);
    assert(lock.drop_creator(caller));
  }
  constexpr M::ForkContinuation unsupported[] = {
      M::ForkContinuation::Ordinary, M::ForkContinuation::NormalDuring,
      M::ForkContinuation::NormalAfter, M::ForkContinuation::LastThreadAfter,
      M::ForkContinuation::LastWorkerAfter};
  for (auto selected : unsupported)
    rejected(selected);
  rejected(initial_owner ? M::ForkContinuation::WorkerThreadDuring
                         : M::ForkContinuation::InitialThreadDuring);
  for (unsigned bad = 0; bad != 5; ++bad) {
    caller.unwind_root = root;
    if (bad == 0) caller.unwind_root.active = false;
    if (bad == 1) caller.unwind_root.owner = &peer;
    if (bad == 2) caller.unwind_root.generation = 0;
    if (bad == 3) caller.unwind_root.chain = 0;
    if (bad == 4)
      caller.unwind_root.kind = M::UnwindRootKind::LastThreadProcess;
    rejected(kind);
  }
  caller.unwind_root = root;
  caller.fork_recovery = I::ForkRecovery::child();
  rejected(kind);
  caller.fork_recovery = I::ForkRecovery{};
  caller.cancellation_suppression.store(0);
  rejected(kind);
  caller.cancellation_suppression.store(2);
  rejected(kind);
  caller.cancellation_suppression.store(1);
  caller.cancellation_transfer.store(1);
  rejected(kind);
  caller.cancellation_transfer.store(0);
  caller.cancellation_point.store(
      reinterpret_cast<M::CancellationPoint *>(&peer));
  rejected(kind);
  caller.cancellation_point.store(nullptr);
  if (!initial_owner) {
    caller.resource_owner = {};
    rejected(kind);
    caller.resource_owner = resource_owner;
    caller.resource_owner.terminal_runner = nullptr;
    rejected(kind);
    caller.resource_owner = resource_owner;
    caller.argument = nullptr;
    rejected(kind);
    caller.argument = &worker;
  }
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.reserve());
  }
  rejected(kind);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.cancel_reservation() && lock.begin_helper_creation());
  }
  rejected(kind);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.cancel_helper_reservation());
    assert(lock.pin(&caller.attributes) == &caller);
  }
  rejected(kind);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.acquire_lease(caller) && lock.unpin(caller));
  }
  rejected(kind);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.release_lease(caller));
    assert(lock.reserve() && lock.insert(peer, false) && lock.start(peer));
    assert(lock.drop_creator(peer));
  }
  rejected(kind);
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.begin_cleanup(peer) && lock.close_leases(peer));
    assert(lock.exit_ready(peer, 0));
    peer.clear_tid.value.store(0);
    assert(lock.pin(&peer.attributes) && lock.claim_join(peer));
    assert(lock.begin_reaping(peer) && lock.unpin(peer));
    assert(lock.finish_reaping(peer));
  }

  bool single = true;
  {
    M::ThreadRegistryLock busy(registry);
    assert(!registry.freeze_for_fork(caller, 1, &single, false, kind, snapshot));
    assert(!single);
    assert(busy.live_threads() == 1 && !busy.process_exiting());
    assert(root.matches(caller.unwind_root));
    assert(caller.cancellation_suppression.load() == 1);
  }
  const bool recoverable = registry.freeze_for_fork(caller, 1, &single, true, kind);
  assert(recoverable == !with_helpers && single == !with_helpers);
  if (recoverable)
    registry.release_after_fork();
  // Even an already helper-free caller explicitly selecting this disposition
  // must not be promoted to a recoverable child by the registry census.
  constexpr bool drained_states[] = {false, true};
  for (bool drained : drained_states) {
    single = true;
    assert(registry.freeze_for_fork(caller, 1, &single, drained, kind, snapshot));
    assert(!single);
    registry.release_after_fork(); // Injected clone-failure rollback.
    assert(root.matches(caller.unwind_root));
    assert(caller.cancellation.terminal());
    assert(caller.cancellation_identity == 9 && caller.cancellation_generation == 7);
    assert(caller.cancellation_suppression.load() == 1);
    assert(caller.retains_resources_until_process_exit() == initial_owner);
    assert(!caller.fork_recovery.is_child());
  }
  assert(registry.freeze_for_fork(caller, 1, &single, false, kind, snapshot));
  assert(!single);
  pid_t child = ::fork();
  assert(child >= 0);
  if (!child) {
    registry.restore_fork_child(caller, I::ForkRecovery::child(), kind, snapshot);
    assert(caller.fork_recovery.is_child() && !caller.fork_recovery.allows_fork());
    assert(!caller.fork_recovery.allows_loader());
    assert(!caller.fork_recovery.allows_thread_creation());
    assert(caller.retains_resources_until_process_exit());
    assert(!caller.begin_callback_cleanup());
    assert(!caller.worker_cleanup_in_progress() && !caller.terminal_cleanup_pending());
    assert(!caller.last_worker_cleanup_finished() && !caller.record_worker_cleanup_finished());
    assert(!caller.unwind_root.active);
    assert(!caller.cancellation.terminal() && !caller.cancellation.ready());
    assert(caller.cancellation.enabled() && caller.cancellation.asynchronous());
    assert(!caller.cancellation_identity && !caller.cancellation_generation);
    assert(!caller.cancellation_point.load() && !caller.cancellation_transfer.load());
    assert(caller.attributes.atexit_callback_mgr == manager);
    assert(caller.resource_owner.context == resource_owner.context);
    assert(caller.resource_owner.retain == resource_owner.retain);
    assert(caller.resource_owner.release == resource_owner.release);
    assert(caller.resource_owner.terminal_runner == resource_owner.terminal_runner);
    if (!initial_owner) {
      assert(caller.control_mapping.base == 0x2000 && caller.control_mapping.size == 8192);
      assert(caller.stack_mapping.base == 0x4000 && caller.stack_mapping.size == 8192);
      assert(caller.stack_bottom == 0x4000 && caller.stack_top == 0x6000);
      assert(caller.tls.addr == 0x6000 && caller.tls.tp == 0x7000 && caller.tls.size == 8192);
      assert(caller.owns_stack && caller.runner.posix_runner == runner);
      assert(caller.argument == &worker);
    }
    {
      M::ThreadRegistryLock lock(registry);
      assert(lock.live_threads() == 1 && !lock.process_exiting());
      assert(!lock.pending_creations() && !lock.pending_helper_creations());
      assert(!lock.queued_reclaims() && !lock.take_reap_candidate());
      assert(lock.owner(caller) == M::ThreadOwner::Joinable);
      assert(lock.callback_phase(caller) == M::CallbackPhase::None);
      assert(lock.leases_drained(caller));
      assert(!lock.reserve() && !lock.begin_helper_creation());
      assert(lock.pin(&caller.attributes) == &caller);
      assert(!lock.acquire_lease(caller) && lock.unpin(caller));
    }
    assert(caller.cancellation_suppression.load() == 1);
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    assert(caller.cancellation_suppression.load() == 0);
    ::_exit(0);
  }
  registry.release_after_fork();
  int status;
  assert(::waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  assert(root.matches(caller.unwind_root) && caller.cancellation.terminal());
  assert(caller.cancellation_suppression.load() == 1);
  assert(caller.cancellation_identity == 9 && caller.cancellation_generation == 7);
  assert(caller.attributes.atexit_callback_mgr == manager);
  assert(!caller.fork_recovery.is_child());
  {
    M::ThreadRegistryLock lock(registry);
    assert(lock.callback_phase(caller) == (initial_owner ? M::CallbackPhase::None
                                                        : M::CallbackPhase::Active));
    assert(lock.owner(caller) == (initial_owner ? M::ThreadOwner::Joinable
                                               : M::ThreadOwner::Detached));
    assert(lock.pin(&caller.attributes) == &caller);
    assert(lock.acquire_lease(caller) && lock.release_lease(caller));
    assert(lock.unpin(caller));
  }
  single = true;
  const bool parent_recoverable = registry.freeze_for_fork(caller, 1, &single, true, kind);
  assert(parent_recoverable == !with_helpers && single == !with_helpers);
  if (parent_recoverable)
    registry.release_after_fork();
  assert(registry.claim_process_cleanup());
  rejected(kind);
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
  for (unsigned mode = 0; mode != 8; ++mode)
    terminal_exec_only_registry(mode & 1, mode & 2, mode & 4);
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
