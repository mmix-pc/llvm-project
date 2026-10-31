//===-- Native MMIX normal-exit fork hook tests
//----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Standalone Linux x86-64 hook fixture, not coordinated-fork qualification.
// Example from the LLVM checkout (without LIBC_FULL_BUILD/public packaging):
// clang++ -std=c++17 -Ilibc -DLIBC_NAMESPACE=exit_fork_test
//   -DLIBC_THREAD_MODE=LIBC_THREAD_MODE_PLATFORM
//   -DLIBC_ERRNO_MODE=LIBC_ERRNO_MODE_THREAD_LOCAL
//   libc/test/src/stdlib/mmix_normal_exit_fork_native.cpp -o
//   /tmp/exit-fork-hooks
// Kernel masks/clear-TID, credentials, TLS drain and process callback dispatch
// are injected. Real host fork isolates cases and checks copied owner state.
// cleanup_current_thread is production code: worker DURING cases promote from
// inside its callback and return through its original worker-branch epilogue.
// Ordinary-root proof and lifecycle/fork integration need separate target
// tests.

#include <cassert>
#include <csignal>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(LIBC_COPT_PUBLIC_PACKAGING) || defined(LIBC_MMIX_ROBUST_MUTEX) ||  \
    defined(LIBC_MMIX_CONCURRENT_STDIO)
#error "Use the standalone, non-public hook-test composition"
#endif

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
// Exercise copied callback scopes without linking a loader provider.
#ifndef LIBC_MMIX_SHARED_STARTUP
#define LIBC_MMIX_SHARED_STARTUP
#endif
#define LIBC_MMIX_UNWIND_ROOTS
#define LIBC_MMIX_CANCELLATION_WINDOWS
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/main_thread.cpp"
#include "src/__support/threads/linux/mmix/thread_reaper.cpp"
#include "src/__support/threads/linux/mmix/thread_finish.cpp"
#include "src/__support/threads/linux/mmix/thread_process_exit.cpp"
#include "src/__support/threads/linux/mmix/fork_runtime.cpp"
#include "src/errno/libc_errno.cpp"
#define __cxa_finalize mmix_normal_exit_test_finalize
#include "src/stdlib/linux/mmix/exit.cpp"
#undef __cxa_finalize

namespace M = LIBC_NAMESPACE::mmix;
namespace I = LIBC_NAMESPACE::internal;

namespace {
enum class Case {
  Return,
  RecursiveProcessExit,
  RecursiveTLSExit,
  Terminal,
  Quick,
  DuringReturn,
  RecursiveDuringChild,
  TerminalDuring,
  CleanupOnly,
  WorkerReturn,
  WorkerDuringReturn,
  WorkerDuringTerminal,
  WorkerCleanupOnly,
  WorkerQuick,
  WorkerTerminal,
  WorkerLiveReaper,
  LastThreadReturn,
  InitialDuringTLS,
  InitialDuringTSS,
  InitialCancelTLS,
  InitialCancelTSS
};
Case selected;
struct ExitStatus {
  int value;
};
uint64_t mask = 0x4000;
unsigned tls_drains, process_drains, registrations;
unsigned during_forks;
bool notifications_closed;
bool recursive_during_child;
unsigned main_drains;
unsigned worker_drain_returns;
bool registration_failure;
bool clone_failure, loader_snapshot, notification_snapshot, allocator_snapshot;
unsigned notification_prepares, drain_roots, after_roots, drain_returns;
unsigned pending_tls, pending_tss, callback_returns;

bool initial_during_case() {
  return selected == Case::InitialDuringTLS || selected == Case::InitialDuringTSS ||
         selected == Case::InitialCancelTLS || selected == Case::InitialCancelTSS;
}

bool cancellation_case() {
  return selected == Case::InitialCancelTLS || selected == Case::InitialCancelTSS;
}

void *terminal_result() {
  return reinterpret_cast<void *>(cancellation_case() ? uintptr_t(-1) : 0x7171);
}

bool worker_case() {
  return selected == Case::WorkerReturn || selected == Case::WorkerQuick ||
         selected == Case::WorkerTerminal || selected == Case::WorkerLiveReaper ||
         selected == Case::WorkerDuringReturn ||
         selected == Case::WorkerDuringTerminal ||
         selected == Case::WorkerCleanupOnly;
}

void wait_success(pid_t child) {
  int status = 0;
  assert(::waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void check_initial_during() {
  auto &caller = *M::current_control;
  assert(caller.terminal_cleanup_in_progress() &&
         !caller.terminal_cleanup_finished());
  assert(I::main_thread_cleanup_in_progress() && !I::main_thread_cleanup_finished());
  assert(M::initial_thread_during_fork_owner() == caller.attributes.tid);
  assert(!I::process_cleanup_tid && !I::normal_exit_fork_owner() &&
         !I::last_thread_exit_fork_owner());
  assert(!notifications_closed && M::thread_registry.admit_signal_notification());
  assert(caller.cancellation.terminal() && caller.cancellation.enabled() &&
         !caller.cancellation.ready());
  assert(caller.cancellation_identity == 19 && caller.cancellation_generation == 23);
  assert(!caller.cancellation_point.load() && !caller.cancellation_transfer.load());
  assert(!caller.cancellation_suppression.load());
  assert(drain_roots == 1 && !after_roots && !drain_returns);
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(lock.live_threads() == 1 && !lock.process_exiting());
  assert(lock.owner(caller) == M::ThreadOwner::Joinable);
  assert(!lock.exit_ready(caller, 0)); // Open leases, still in the drain.
  assert(lock.reserve() && lock.cancel_reservation());
}

void fork_initial_continuation(bool during) {
  auto &caller = *M::current_control;
  auto *attributes = I::self.attrib;
  auto *manager = attributes->atexit_callback_mgr;
  const auto root = caller.unwind_root;
  const auto scopes = caller.loader_scopes.load();
  for (unsigned generation = 0; generation != 2; ++generation) {
    const pid_t owner = attributes->tid;
    const auto saved_mask = mask;
    const auto registered = registrations;
    LIBC_NAMESPACE::libc_errno = 73;
    if (during) check_initial_during();
    // Exercise the real runtime's failed-clone restoration, not a manual reset.
    {
      M::ForkOperation operation;
      assert(!operation.error() && !operation.prepare_runtime());
      assert(!operation.freeze_registry());
      clone_failure = true;
      assert(operation.create_child() == -EAGAIN);
      clone_failure = false;
      operation.restore_runtime(false);
    }
    assert(!loader_snapshot && !notification_snapshot && !allocator_snapshot);
    assert(attributes->tid == owner && registrations == registered);
    assert(root.matches(caller.unwind_root) && mask == saved_mask);
    if (during) check_initial_during();
    long child;
    {
      M::ForkOperation operation;
      assert(!operation.error() && !operation.prepare_runtime());
      assert(!operation.freeze_registry());
      child = operation.create_child();
      assert(child >= 0);
      if (!child) ::alarm(10);
      operation.restore_runtime(child == 0);
    }
    assert(mask == saved_mask && int(LIBC_NAMESPACE::libc_errno) == 73);
    assert(root.matches(caller.unwind_root));
    assert(I::self.attrib == attributes && attributes->atexit_callback_mgr == manager);
    assert(caller.loader_scopes.load() == scopes);
    assert(!loader_snapshot && !notification_snapshot && !allocator_snapshot);
    assert(!caller.cancellation_suppression.load());
    assert(caller.cancellation.terminal() && !caller.cancellation.ready());
    if (during) check_initial_during();
    else {
      assert(!M::initial_thread_during_fork_owner());
      assert(I::last_thread_exit_fork_owner() == attributes->tid);
      assert(notifications_closed && caller.terminal_cleanup_finished());
    }
    if (child) {
      wait_success(pid_t(child));
      assert(attributes->tid == owner && registrations == registered);
      return;
    }
    assert(attributes->tid != owner && registrations == registered + 1);
    if (during) ++during_forks;
  }
}

void initial_during_callback() {
  auto &caller = *M::current_control;
  auto *attributes = I::self.attrib;
  auto *manager = attributes->atexit_callback_mgr;
  const auto root = caller.unwind_root;
  const auto owner = attributes->tid;
  check_initial_during();
  for (unsigned bad = 0; bad != 11; ++bad) {
    if (bad == 0) ++caller.unwind_root.generation;
    if (bad == 1) caller.unwind_root.owner = nullptr;
    if (bad == 2) caller.unwind_root.active = false;
    if (bad == 3) ++caller.unwind_root.chain;
    if (bad == 4) ++caller.unwind_root.ro;
    if (bad == 5) ++caller.unwind_root.sp;
    if (bad == 6) ++caller.unwind_root.procedure;
    if (bad == 7) caller.unwind_root.kind = M::UnwindRootKind::Thread;
    if (bad == 8) attributes->atexit_callback_mgr = nullptr;
    if (bad == 9) I::self.attrib = nullptr;
    if (bad == 10) attributes->tid = owner + 1;
    assert(!M::initial_thread_during_fork_owner());
    assert(!M::rebind_initial_thread_during_fork_owner(owner));
    {
      M::ForkOperation operation;
      assert(!operation.error() && operation.prepare_runtime() == EAGAIN);
    }
    caller.unwind_root = root;
    I::self.attrib = attributes;
    attributes->atexit_callback_mgr = manager;
    attributes->tid = owner;
  }
  // Early bootstrap absence must precede notification preparation.
  const auto prepared = notification_prepares;
  assert(M::bootstrap.mutex.try_lock());
  {
    M::ForkOperation operation;
    assert(!operation.error() && operation.prepare_runtime() == EAGAIN);
  }
  M::bootstrap.mutex.unlock();
  assert(notification_prepares == prepared);
  // Frozen revalidation is local even with loader metadata locked.
  for (unsigned bad = 0; bad != 2; ++bad) {
    M::ForkOperation operation;
    assert(!operation.error() && !operation.prepare_runtime());
    if (bad == 0) ++caller.unwind_root.generation;
    else attributes->atexit_callback_mgr = nullptr;
    assert(operation.freeze_registry() == EAGAIN);
    caller.unwind_root = root;
    attributes->atexit_callback_mgr = manager;
  }
  // No admission by erasing a live point, transfer or foreign suppression.
  for (unsigned bad = 0; bad != 3; ++bad) {
    if (bad == 0) caller.cancellation_transfer.store(1);
    if (bad == 1) caller.cancellation_point.store(
        reinterpret_cast<M::CancellationPoint *>(8));
    if (bad == 2) caller.cancellation_suppression.store(1);
    { M::ForkOperation operation; assert(operation.error() == EAGAIN); }
    assert(caller.cancellation_transfer.load() == (bad == 0));
    assert(bool(caller.cancellation_point.load()) == (bad == 1));
    assert(caller.cancellation_suppression.load() == (bad == 2));
    caller.cancellation_transfer.store(0);
    caller.cancellation_point.store(nullptr);
    caller.cancellation_suppression.store(0);
  }
  fork_initial_continuation(true);
  assert(!I::adopt_fork_survivor_after_thread_exit());
  assert(!I::adopt_fork_survivor_during_cleanup());
  assert(!I::rebind_normal_exit_fork_owner(owner));
  assert(!I::rebind_last_thread_exit_fork_owner(owner));
  check_initial_during();
  ++callback_returns;
}

void check_terminal_child_state(M::ThreadControl &caller) {
  assert(caller.terminal_cleanup_finished());
  assert(caller.cancellation.terminal() && !caller.cancellation.ready());
  assert(!caller.cancellation.enabled() && caller.cancellation.asynchronous());
  assert(!caller.cancellation.request());
  assert(caller.cancellation_identity == 19 &&
         caller.cancellation_generation == 23);
  assert(!M::thread_registry.admit_signal_notification());
  assert(!M::thread_registry.claim_process_cleanup());
  assert(!caller.begin_callback_cleanup());
  I::cleanup_current_thread();
  assert(tls_drains == 1 && I::main_thread_cleanup_finished());
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(lock.live_threads() == 0 && lock.leases_drained(caller));
  assert(lock.owner(caller) == M::ThreadOwner::Joinable);
  assert(!lock.reserve() && !lock.begin_helper_creation());
  assert(!lock.begin_cleanup(caller) && !lock.exit_ready(caller, 0));
  assert(!lock.claim_process_exit());
  assert(lock.request_cancellation(caller, true) == ESRCH);
  assert(lock.pin(&caller.attributes) == &caller);
  assert(!lock.acquire_lease(caller));
  assert(lock.unpin(caller));
}

void fork_after_last_thread_cleanup() {
  auto &caller = *M::current_control;
  constexpr auto kind = M::ForkContinuation::LastThreadAfter;
  auto *attributes = I::self.attrib;
  auto *manager = attributes->atexit_callback_mgr;
  const auto root = caller.unwind_root;
  assert(!I::normal_exit_fork_owner());
  assert(I::last_thread_exit_fork_owner() == attributes->tid);
  assert(notifications_closed && tls_drains == 1);
  check_terminal_child_state(caller);

  // A phase or terminal bit alone must not authorize a different activation.
  for (unsigned bad = 0; bad != 8; ++bad) {
    caller.unwind_root = root;
    switch (bad) {
    case 0:
      ++caller.unwind_root.generation;
      break;
    case 1:
      caller.unwind_root.owner = nullptr;
      break;
    case 2:
      caller.unwind_root.active = false;
      break;
    case 3:
      ++caller.unwind_root.chain;
      break;
    case 4:
      caller.unwind_root.kind = M::UnwindRootKind::Thread;
      break;
    case 5:
      ++caller.unwind_root.ro;
      break;
    case 6:
      ++caller.unwind_root.sp;
      break;
    case 7:
      ++caller.unwind_root.procedure;
      break;
    }
    assert(!I::last_thread_exit_fork_owner());
    assert(!I::rebind_last_thread_exit_fork_owner(attributes->tid));
  }
  caller.unwind_root = root;
  auto phase = I::normal_exit_phase;
  I::normal_exit_phase = I::NormalExitPhase::ThreadCallbacks;
  assert(!I::last_thread_exit_fork_owner());
  I::normal_exit_phase = phase;
  I::last_thread_attributes = nullptr;
  assert(!I::last_thread_exit_fork_owner());
  I::last_thread_attributes = attributes;
  assert(!I::adopt_fork_survivor_after_cleanup());
  assert(!I::adopt_fork_survivor_during_cleanup());

  for (unsigned round = 0; round != 2; ++round) {
    const pid_t owner = attributes->tid;
    const unsigned registered = registrations;
    const auto saved_mask = mask;
    bool single = true;
    LIBC_NAMESPACE::libc_errno = 73;
    assert(caller.cancellation_suppression.fetch_add(1) == 0);
    assert(
        !M::thread_registry.freeze_for_fork(caller, 1, &single, false, kind));
    assert(!single);
    assert(M::bootstrap.mutex.try_lock());
    assert(!M::reaper_absent_for_exit_fork(caller, kind));
    M::bootstrap.mutex.unlock();
    assert(M::reaper_absent_for_exit_fork(caller, kind));
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, true, kind));
    assert(single);
    // Failed-clone rollback model: no child restore, no phase/root mutation.
    M::thread_registry.release_after_fork();
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    check_terminal_child_state(caller);
    assert(I::last_thread_exit_fork_owner() == owner);
    assert(root.matches(caller.unwind_root) && mask == saved_mask);
    assert(registrations == registered &&
           int(LIBC_NAMESPACE::libc_errno) == 73);

    assert(caller.cancellation_suppression.fetch_add(1) == 0);
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, true, kind));
    pid_t child = ::fork();
    assert(child >= 0);
    if (child) {
      M::thread_registry.release_after_fork();
      assert(caller.cancellation_suppression.fetch_sub(1) == 1);
      wait_success(child);
      check_terminal_child_state(caller);
      assert(attributes->tid == owner && registrations == registered);
      assert(root.matches(caller.unwind_root));
      assert(I::last_thread_exit_fork_owner() == owner && mask == saved_mask);
      return;
    }
    ::alarm(10);
    attributes->tid = ::getpid();
    assert(!I::last_thread_exit_fork_owner());
    M::thread_registry.restore_fork_child(
        caller, I::ForkRecovery::child(true, true), kind);
    assert(!I::adopt_fork_survivor_after_cleanup());
    assert(!I::adopt_fork_survivor_during_cleanup());
    const auto recovery = caller.fork_recovery;
    // A nonquiescent loader must not yield a terminal resumable child.
    caller.fork_recovery = I::ForkRecovery::child(true, false);
    assert(!I::adopt_fork_survivor_after_thread_exit());
    assert(!I::rebind_last_thread_exit_fork_owner(owner));
    caller.fork_recovery = recovery;
    registration_failure = true;
    assert(!I::adopt_fork_survivor_after_thread_exit());
    assert(caller.terminal_cleanup_finished() &&
           I::main_thread_cleanup_finished());
    registration_failure = false;
    assert(I::adopt_fork_survivor_after_thread_exit());
    assert(!I::rebind_last_thread_exit_fork_owner(0));
    assert(!I::rebind_last_thread_exit_fork_owner(attributes->tid));
    assert(!I::rebind_last_thread_exit_fork_owner(owner == 1 ? 2 : 1));
    assert(I::rebind_last_thread_exit_fork_owner(owner));
    assert(!I::rebind_last_thread_exit_fork_owner(owner));
    assert(I::last_thread_exit_fork_owner() == attributes->tid);
    assert(I::self.attrib == attributes &&
           attributes->atexit_callback_mgr == manager);
    assert(registrations == registered + 2 && mask == saved_mask);
    assert(root.matches(caller.unwind_root));
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    check_terminal_child_state(caller);
  }
  // Both descendants return through the copied process-finalization call.
}

void fork_during_cleanup() {
  auto &caller = *M::current_control;
  auto *attributes = I::self.attrib;
  auto *manager = attributes->atexit_callback_mgr;
  const unsigned rounds = selected == Case::DuringReturn ? 2 : 1;
  for (unsigned round = 0; round != rounds; ++round) {
    const pid_t owner = caller.attributes.tid;
    const unsigned registered = registrations;
    bool during = false;
    assert(I::normal_exit_fork_owner(&during) == owner && during);
    bool single = false;
    // Exercise registry rollback without a clone, then freeze the real copy.
    // This fixture injects the private certificate, not the ordinary-root
    // proof.
    assert(caller.cancellation_suppression.fetch_add(1) == 0);
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, false, M::ForkContinuation::NormalDuring));
    assert(single);
    M::thread_registry.release_after_fork();
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    assert(I::normal_exit_fork_owner() == owner);
    assert(I::main_thread_cleanup_in_progress());
    I::cleanup_current_thread();
    assert(tls_drains == 1 && registrations == registered);
    assert(!M::thread_registry.claim_process_cleanup());

    assert(caller.cancellation_suppression.fetch_add(1) == 0);
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, false, M::ForkContinuation::NormalDuring));
    assert(single);
    pid_t child = ::fork();
    assert(child >= 0);
    if (child) {
      M::thread_registry.release_after_fork();
      assert(caller.cancellation_suppression.fetch_sub(1) == 1);
      wait_success(child);
      assert(caller.attributes.tid == owner);
      assert(I::normal_exit_fork_owner() == owner);
      assert(I::main_thread_cleanup_in_progress());
      assert(!I::main_thread_cleanup_finished());
      assert(I::self.attrib == attributes &&
             attributes->atexit_callback_mgr == manager);
      assert(registrations == registered && tls_drains == 1);
      assert(!M::thread_registry.claim_process_cleanup());
      return;
    }

    ::alarm(10);
    caller.attributes.tid = ::getpid();
    assert(!I::normal_exit_fork_owner());
    if (!caller.fork_recovery.is_child())
      assert(!I::rebind_normal_exit_fork_owner(owner));
    caller.fork_recovery = I::ForkRecovery::child();
    assert(!I::rebind_normal_exit_fork_owner(owner));
    assert(!I::adopt_fork_survivor_during_cleanup());
    assert(!I::adopt_fork_survivor_after_cleanup());
    assert(registrations == registered);

    M::thread_registry.restore_fork_child(
        caller, I::ForkRecovery::child(true, true), M::ForkContinuation::NormalDuring);
    assert(!I::adopt_fork_survivor_after_cleanup());
    assert(I::adopt_fork_survivor_during_cleanup());
    assert(!I::rebind_normal_exit_fork_owner(0));
    assert(!I::rebind_normal_exit_fork_owner(-1));
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    assert(!I::rebind_normal_exit_fork_owner(owner == 1 ? 2 : 1));
    I::normal_exit_phase = I::NormalExitPhase::ProcessCallbacks;
    assert(!I::rebind_normal_exit_fork_owner(owner));
    I::normal_exit_phase = I::NormalExitPhase::ThreadCallbacks;
    assert(I::rebind_normal_exit_fork_owner(owner));
    assert(!I::rebind_normal_exit_fork_owner(owner));
    assert(I::normal_exit_fork_owner() == caller.attributes.tid);
    assert(I::main_thread_cleanup_in_progress());
    assert(!I::main_thread_cleanup_finished());
    assert(I::self.attrib == attributes &&
           attributes->atexit_callback_mgr == manager);
    assert(registrations == registered + 1 && notifications_closed);
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    ++during_forks;
    I::cleanup_current_thread();
    assert(tls_drains == 1 && process_drains == 0);
    assert(!M::thread_registry.claim_process_cleanup());
    if (selected == Case::RecursiveDuringChild) {
      recursive_during_child = true;
      I::exit_process(29);
    }
  }
  // Return through the copied TLS/TSS drain and then normal process callbacks.
}

void fork_after_worker_cleanup() {
  auto &caller = *M::current_control;
  auto *attributes = I::self.attrib;
  auto *manager = attributes->atexit_callback_mgr;
  auto *initial_attributes = I::active_attributes;
  assert(initial_attributes != attributes && I::phase == I::Phase::Finished);
  assert(!caller.retains_resources_until_process_exit());
  assert(main_drains == 1 && tls_drains == 1 && notifications_closed);
  const auto root = caller.unwind_root;
  const auto stack = caller.stack_mapping;
  const auto tls = caller.shared_tls;
  const auto saved_mask = mask;
  LIBC_NAMESPACE::libc_errno = 73;
  // Model the scope held by a dynamic process callback. No helper retirement
  // or loader operation is allowed from the absence witness.
  caller.loader_scopes.store(1);
  caller.loader_callbacks.store(1);
  assert(!M::retire_reaper_for_fork(caller));
  assert(!M::reaper_absent_for_exit_fork(caller)); // Missing suppression.
  assert(caller.cancellation_suppression.fetch_add(1) == 0);
  if (selected == Case::WorkerLiveReaper) {
    assert(!M::reaper_absent_for_exit_fork(caller));
    assert(M::bootstrap.attributes && M::bootstrap.control);
    assert(!M::bootstrap.stopping.load());
    bool single = true;
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, false, M::ForkContinuation::NormalAfter));
    assert(!single);
    M::thread_registry.release_after_fork();
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    return;
  }
  assert(M::bootstrap.mutex.try_lock());
  assert(!M::reaper_absent_for_exit_fork(caller));
  M::bootstrap.mutex.unlock();
  M::ThreadControl unfinished;
  M::bootstrap.attributes = &unfinished.attributes;
  assert(!M::reaper_absent_for_exit_fork(caller));
  M::bootstrap.attributes = nullptr;
  M::bootstrap.control = &unfinished;
  assert(!M::reaper_absent_for_exit_fork(caller));
  M::bootstrap.control = nullptr;
  M::bootstrap.stopping.store(true);
  assert(!M::reaper_absent_for_exit_fork(caller));
  M::bootstrap.stopping.store(false);
  assert(M::reaper_absent_for_exit_fork(caller));

  for (unsigned round = 0; round != 2; ++round) {
    const pid_t owner = caller.attributes.tid;
    const unsigned registered = registrations;
    const bool worker = !caller.retains_resources_until_process_exit();
    auto *old_active = I::active_attributes;
    bool single = true;
    if (worker) {
      assert(M::thread_registry.freeze_for_fork(caller, 1, &single, false,
                                               M::ForkContinuation::NormalAfter));
      assert(!single); // Warm-up retirement must not erase parent history.
      M::thread_registry.release_after_fork();
    }
    assert(M::reaper_absent_for_exit_fork(caller));
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, true, M::ForkContinuation::NormalAfter));
    assert(single);
    // Model clone failure by releasing the snapshot without child dispatch.
    // This is private-hook rollback coverage, not ForkOperation transport.
    M::thread_registry.release_after_fork();
    assert(I::normal_exit_fork_owner() == owner);
    assert(I::active_attributes == old_active &&
           I::phase == I::Phase::Finished);
    assert(registrations == registered && mask == saved_mask);
    assert(LIBC_NAMESPACE::libc_errno == 73 && notifications_closed);
    assert(!caller.begin_callback_cleanup() &&
           !M::thread_registry.claim_process_cleanup());

    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, true, M::ForkContinuation::NormalAfter));
    assert(single);
    pid_t child = ::fork();
    assert(child >= 0);
    if (child) {
      M::thread_registry.release_after_fork();
      wait_success(child);
      assert(I::normal_exit_fork_owner() == owner);
      assert(caller.retains_resources_until_process_exit() == !worker);
      assert(I::active_attributes == old_active &&
             I::phase == I::Phase::Finished);
      assert(registrations == registered && mask == saved_mask);
      assert(LIBC_NAMESPACE::libc_errno == 73 && notifications_closed);
      assert(I::self.attrib == attributes &&
             attributes->atexit_callback_mgr == manager);
      assert(main_drains == 1 && tls_drains == 1 && process_drains == 1);
      assert(caller.cancellation_suppression.fetch_sub(1) == 1);
      return;
    }
    ::alarm(10);
    caller.attributes.tid = ::getpid();
    assert(!I::normal_exit_fork_owner());
    caller.fork_recovery = I::ForkRecovery::child();
    assert(!I::adopt_fork_survivor_after_cleanup(worker));
    assert(!I::rebind_normal_exit_fork_owner(owner));
    M::thread_registry.restore_fork_child(
        caller, I::ForkRecovery::child(true, true), M::ForkContinuation::NormalAfter);
    if (worker) {
      assert(!I::adopt_fork_survivor_after_cleanup());
      assert(!I::adopt_fork_survivor_during_cleanup());
      assert(!I::rebind_normal_exit_fork_owner(owner));
    }
    I::self.attrib = initial_attributes;
    assert(!I::adopt_fork_survivor_after_cleanup(worker));
    I::self.attrib = attributes;
    registration_failure = true;
    assert(!I::adopt_fork_survivor_after_cleanup(worker));
    assert(I::active_attributes == old_active);
    registration_failure = false;
    assert(I::adopt_fork_survivor_after_cleanup(worker));
    assert(I::main_thread_cleanup_finished() &&
           !I::main_thread_cleanup_in_progress());
    assert(I::active_attributes == attributes &&
           registrations == registered + 2);
    assert(!I::rebind_normal_exit_fork_owner(0));
    assert(!I::rebind_normal_exit_fork_owner(-1));
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    I::normal_exit_phase = I::NormalExitPhase::ThreadCallbacks;
    assert(!I::rebind_normal_exit_fork_owner(owner));
    I::normal_exit_phase = I::NormalExitPhase::ProcessCallbacks;
    assert(I::rebind_normal_exit_fork_owner(owner));
    assert(!I::rebind_normal_exit_fork_owner(owner));
    caller.cancellation.restore_after_fork();
    assert(!caller.cancellation.enabled() &&
           caller.cancellation.asynchronous());
    assert(!caller.cancellation.terminal());
    assert(caller.unwind_root.owner == root.owner && caller.unwind_root.active);
    assert(caller.unwind_root.chain == root.chain &&
           caller.unwind_root.generation == root.generation);
    assert(caller.stack_mapping.base == stack.base &&
           caller.stack_mapping.size == stack.size);
    assert(caller.shared_tls.tp == tls.tp &&
           caller.shared_tls.runtime_owner == tls.runtime_owner);
    assert(caller.loader_scopes.load() == 1 &&
           caller.loader_callbacks.load() == 1);
    assert(attributes->atexit_callback_mgr == manager);
    I::cleanup_current_thread();
    assert(main_drains == 1 && tls_drains == 1 && process_drains == 1);
    assert(!M::thread_registry.claim_process_cleanup());
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(!lock.reserve() && !lock.begin_helper_creation());
    }
  }
  assert(caller.cancellation_suppression.fetch_sub(1) == 1);
  // Return through both copied process-finalizer frames, never call exit again.
}

void fork_worker_continuation(bool during) {
  auto &caller = *M::current_control;
  auto *attributes = I::self.attrib;
  auto *manager = attributes->atexit_callback_mgr;
  const auto root = caller.unwind_root;
  const auto stack = caller.stack_mapping;
  const auto tls = caller.shared_tls;
  const auto scopes = caller.loader_scopes.load();
  auto *draining = I::worker_draining;
  auto *completed = I::worker_cleanup;
  const auto saved_mask = mask;
  LIBC_NAMESPACE::libc_errno = 73;
  assert(worker_drain_returns == (during ? 0u : 1u));
  for (unsigned round = 0; round != (during ? 2u : 1u); ++round) {
    const bool worker = !caller.retains_resources_until_process_exit();
    const pid_t owner = caller.attributes.tid;
    const unsigned registered = registrations;
    auto *old_active = I::active_attributes;
    const auto old_phase = I::phase;
    bool snapshot_during = !during;
    assert(I::normal_exit_fork_owner(&snapshot_during) == owner);
    assert(snapshot_during == during);
    const bool cleanup_during = snapshot_during;
    const auto kind = cleanup_during ? M::ForkContinuation::NormalDuring
                                    : M::ForkContinuation::NormalAfter;
    assert(I::main_thread_cleanup_in_progress() == (during && !worker));
    assert(I::main_thread_cleanup_finished() == (!during && !worker));
    I::cleanup_current_thread();
    assert(tls_drains == 1 && worker_drain_returns == (during ? 0u : 1u));

    assert(caller.cancellation_suppression.fetch_add(1) == 0);
    assert(M::reaper_absent_for_exit_fork(caller, kind));
    bool single = false;
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, true, kind));
    assert(single);
    // Model clone failure without changing the suspended worker activation.
    M::thread_registry.release_after_fork();
    assert(I::active_attributes == old_active && I::phase == old_phase);
    assert(I::normal_exit_fork_owner() == owner);
    assert(caller.retains_resources_until_process_exit() == !worker);
    assert(!M::thread_registry.claim_process_cleanup());
    assert(M::thread_registry.freeze_for_fork(caller, 1, &single, true, kind));
    assert(single);
    pid_t child = ::fork();
    assert(child >= 0);
    if (child) {
      M::thread_registry.release_after_fork();
      assert(caller.cancellation_suppression.fetch_sub(1) == 1);
      wait_success(child);
      assert(I::active_attributes == old_active && I::phase == old_phase);
      assert(caller.retains_resources_until_process_exit() == !worker);
      assert(I::normal_exit_fork_owner(&snapshot_during) == owner &&
             snapshot_during == during);
      assert(registrations == registered && mask == saved_mask);
      assert(LIBC_NAMESPACE::libc_errno == 73);
      assert(caller.loader_scopes.load() == scopes);
      assert(I::worker_draining == draining && I::worker_cleanup == completed);
      assert(!caller.cancellation.enabled() && caller.cancellation.asynchronous());
      return;
    }
    ::alarm(10);
    caller.attributes.tid = ::getpid();
    snapshot_during = true;
    assert(!I::normal_exit_fork_owner(&snapshot_during) && !snapshot_during);
    M::thread_registry.restore_fork_child(
        caller, I::ForkRecovery::child(true, true), kind);
    const auto recovery = caller.fork_recovery;
    caller.fork_recovery = I::ForkRecovery::child();
    assert(!I::adopt_fork_survivor_during_cleanup(worker));
    assert(!I::adopt_fork_survivor_after_cleanup(worker));
    assert(!I::rebind_normal_exit_fork_owner(owner));
    caller.fork_recovery = recovery;
    I::self.attrib = nullptr;
    assert(!I::adopt_fork_survivor_during_cleanup(worker));
    assert(!I::adopt_fork_survivor_after_cleanup(worker));
    assert(!I::rebind_normal_exit_fork_owner(owner));
    I::self.attrib = attributes;
    assert(registrations == registered);
    assert(I::active_attributes == old_active && I::phase == old_phase);
    if (worker) {
      assert(!I::adopt_fork_survivor_during_cleanup());
      assert(!I::adopt_fork_survivor_after_cleanup());
      assert(!I::rebind_normal_exit_fork_owner(owner));
    }
    if (worker && during) {
      registration_failure = true;
      assert(!I::adopt_fork_survivor_during_cleanup(true));
      assert(I::active_attributes == old_active && I::phase == old_phase);
      registration_failure = false;
    }
    // Select from the query taken before clone, never from promoted identity.
    assert(cleanup_during ? I::adopt_fork_survivor_during_cleanup(worker)
                         : I::adopt_fork_survivor_after_cleanup(worker));
    assert(I::main_thread_cleanup_in_progress() == during);
    assert(I::main_thread_cleanup_finished() == !during);
    assert(!I::rebind_normal_exit_fork_owner(0));
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    assert(I::rebind_normal_exit_fork_owner(owner));
    assert(!I::rebind_normal_exit_fork_owner(owner));
    assert(I::normal_exit_fork_owner(&snapshot_during) == caller.attributes.tid &&
           snapshot_during == during);
    assert(registrations == registered + (worker && during ? 2u : 1u));
    caller.cancellation.restore_after_fork();
    assert(!caller.cancellation.enabled() && caller.cancellation.asynchronous());
    assert(caller.loader_scopes.load() == scopes &&
           !caller.loader_callbacks.load());
    assert(I::worker_draining == draining && I::worker_cleanup == completed);
    assert(caller.unwind_root.owner == root.owner && caller.unwind_root.active &&
           caller.unwind_root.chain == root.chain &&
           caller.unwind_root.generation == root.generation);
    assert(caller.stack_mapping.base == stack.base &&
           caller.stack_mapping.size == stack.size);
    assert(caller.shared_tls.tp == tls.tp &&
           caller.shared_tls.runtime_owner == tls.runtime_owner);
    assert(I::self.attrib == attributes &&
           attributes->atexit_callback_mgr == manager);
    assert(mask == saved_mask && LIBC_NAMESPACE::libc_errno == 73);
    assert(caller.cancellation_suppression.fetch_sub(1) == 1);
    I::cleanup_current_thread();
    assert(tls_drains == 1 && main_drains == 1 &&
           worker_drain_returns == (during ? 0u : 1u));
    assert(I::main_thread_cleanup_in_progress() == during);
    assert(!M::thread_registry.claim_process_cleanup() && notifications_closed);
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(!lock.reserve() && !lock.begin_helper_creation());
    }
    if (during)
      ++during_forks;
  }
  // DURING returns into the actual cleanup_current_thread worker branch.
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
class ThreadAtExitCallbackMgr {};
FILE *stdin = nullptr, *stdout = nullptr, *stderr = nullptr;
Mutex File::list_lock(false, false, false, false);
File *File::get_first_file() { return nullptr; }
void File::unlock_list() { list_lock.unlock(); }
namespace mmix {
// This fixture models only the private priority-ledger snapshot lock, not PI
// mutex behavior. The real lock implementation has independent target tests.
MutexError RobustMutex::acquire(cpp::optional<Futex::Timeout> timeout,
                               bool try_only) {
  assert(this == &current_control->priority_protection.guard);
  assert(!timeout && !try_only && !lock_count);
  lock_count = 1;
  return MutexError::NONE;
}
MutexError RobustMutex::unlock() {
  assert(this == &current_control->priority_protection.guard && lock_count == 1);
  lock_count = 0;
  return MutexError::NONE;
}
bool initialize_credentials(ThreadControl &) { return true; }
void close_notification_admission() { notifications_closed = true; }
CredentialGate::CredentialGate() { __builtin_trap(); }
CredentialGate::~CredentialGate() { __builtin_trap(); }
void unregister_credentials(ThreadControl &) { __builtin_trap(); }
bool prepare_credentials_fork() { return true; }
void finish_credentials_fork() {}
void restore_credentials_after_fork(ThreadControl &) {}
void once_after_fork(bool quiescent) { assert(quiescent); }
void allocator_fork_prepare() {
  assert(!allocator_snapshot);
  allocator_snapshot = true;
}
void allocator_fork_release() {
  assert(allocator_snapshot);
  allocator_snapshot = false;
}
int prepare_loader_fork(bool *quiescent) {
  assert(!loader_snapshot);
  loader_snapshot = true;
  *quiescent = true;
  return 0;
}
void finish_loader_fork(bool, bool) {
  assert(loader_snapshot);
  loader_snapshot = false;
}
int prepare_notification_fork(bool minimal, bool *drained) {
  assert(!minimal && !notification_snapshot);
  ++notification_prepares;
  notification_snapshot = true;
  *drained = true;
  return 0;
}
void finish_notification_fork(bool) {
  assert(notification_snapshot);
  notification_snapshot = false;
}
ForkResult fork_process(bool wait_for_exec) {
  assert(!wait_for_exec);
  const auto owner = current_control->attributes.tid;
  if (clone_failure) return {-EAGAIN, owner};
  pid_t child = ::fork();
  assert(child >= 0);
  if (!child) current_control->attributes.tid = ::getpid();
  return {child, owner};
}
// Root capture/walking is separately tested with cursor injection. These
// boundaries make the real ForkOperation/finish/certificate flow executable
// on the host, including loader-before-metadata ordering and no window reset.
bool has_c_cleanup() { return false; }
bool ordinary_fork_context(ThreadControl &c) {
  assert(!loader_snapshot);
  return current_control == &c && c.unwind_root.active;
}
bool terminal_thread_fork_context(ThreadControl &c) {
  return ordinary_fork_context(c) && c.terminal_cleanup_in_progress() &&
         c.unwind_root.kind == UnwindRootKind::TerminalThreadCleanup;
}
bool last_thread_fork_context(ThreadControl &c) {
  return ordinary_fork_context(c) && c.terminal_cleanup_finished() &&
         c.unwind_root.kind == UnwindRootKind::LastThreadProcess;
}
bool last_worker_fork_context(ThreadControl &c) {
  return ordinary_fork_context(c) && c.last_worker_cleanup_finished() &&
         c.unwind_root.kind == UnwindRootKind::LastThreadProcess;
}
bool restore_fork_unwind(ThreadControl &c) {
  assert(!loader_snapshot);
  return c.unwind_root.active;
}
bool restore_fork_cancellation(ThreadControl &) { __builtin_trap(); }
void run_with_unwind_root(ThreadControl &, RootBody *, void *) { __builtin_trap(); }
void run_with_terminal_cleanup_root(ThreadControl &c, RootBody *body, void *arg) {
  assert(!c.unwind_root.active && c.terminal_cleanup_pending());
  assert(!initial_thread_during_fork_owner() && !drain_roots++);
  c.unwind_root = {&c, 8, 16, 24, 4, c.unwind_root.generation + 1, true,
                   UnwindRootKind::TerminalThreadCleanup};
  body(arg);
  assert(!initial_thread_during_fork_owner() && I::main_thread_cleanup_finished());
  assert(c.unwind_root.active && !drain_returns++);
  c.unwind_root.active = false;
}
[[noreturn]] void run_with_process_exit_root(ThreadControl &c, RootBody *body,
                                             void *arg) {
  assert(!c.unwind_root.active && c.terminal_cleanup_finished());
  assert(!initial_thread_during_fork_owner() && drain_returns == 1 && !after_roots++);
  c.unwind_root = {&c, 8, 16, 24, 4, c.unwind_root.generation + 1, true,
                   UnwindRootKind::LastThreadProcess};
  body(arg);
  __builtin_trap();
}
extern "C" [[noreturn]] void __llvm_libc_mmix_thread_exit() { __builtin_trap(); }
ThreadCreationResult create_thread_helper(const ThreadPreparation &,
                                         ThreadAttributes *&,
                                         ThreadControl **) {
  __builtin_trap();
}
long reclaim_thread(ThreadControl &, ThreadOwner, ThreadResources &) {
  __builtin_trap(); // Absence certification must never release resources.
}
extern "C" void __llvm_libc_mmix_reaper_loop() { __builtin_trap(); }
} // namespace mmix
namespace internal {
EnvironmentManager &EnvironmentManager::get_instance() {
  static EnvironmentManager manager;
  return manager;
}
ThreadAtExitCallbackMgr *get_thread_atexit_callback_mgr() {
  static ThreadAtExitCallbackMgr manager;
  return &manager;
}
void call_atexit_callbacks(ThreadAttributes *attributes) {
  if (initial_during_case()) {
    assert(attributes == self.attrib && !tls_drains++);
    const bool tls = selected == Case::InitialDuringTLS ||
                     selected == Case::InitialCancelTLS;
    // Model the manager's live callback and pending cursor on this returning
    // stack. TSS entry deliberately has no TLS loader scope left to drain.
    assert(M::current_control->loader_scopes.fetch_add(1) == 0);
    unsigned cursor = 1;
    if (tls) initial_during_callback();
    assert(cursor++ == 1 && !pending_tls++);
    assert(M::current_control->loader_scopes.fetch_sub(1) == 1);
    if (!tls) initial_during_callback();
    assert(cursor++ == 2 && !pending_tss++);
    assert(callback_returns == 1 && !M::current_control->loader_scopes.load());
    return;
  }
  if (worker_case()) {
    assert(attributes == self.attrib);
    if (M::current_control->retains_resources_until_process_exit()) {
      assert(!normal_exit_fork_owner());
      assert(++main_drains == 1 && main_thread_cleanup_in_progress());
      return;
    }
    assert(++tls_drains == 1 && main_drains == 1);
    assert(!main_thread_cleanup_in_progress() &&
           !main_thread_cleanup_finished());
    auto old_phase = normal_exit_phase;
    normal_exit_phase = NormalExitPhase::ProcessCallbacks;
    assert(!normal_exit_fork_owner()); // Phase alone is not a completed drain.
    normal_exit_phase = old_phase;
    bool during = true;
    if (selected == Case::WorkerQuick || selected == Case::WorkerTerminal ||
        selected == Case::WorkerCleanupOnly) {
      assert(!normal_exit_fork_owner(&during) && !during);
      return;
    }
    assert(normal_exit_fork_owner(&during) == attributes->tid && during);
    auto *draining = worker_draining;
    worker_draining = nullptr;
    assert(!normal_exit_fork_owner(&during) && !during);
    worker_draining = draining;
    auto &caller = *M::current_control;
    M::ThreadControl other;
    other.attributes.tid = attributes->tid;
    M::current_control = &other;
    self.attrib = &other.attributes;
    during = true;
    assert(!normal_exit_fork_owner(&during) && !during);
    M::current_control = &caller;
    during = true;
    assert(!normal_exit_fork_owner(&during) && !during);
    self.attrib = attributes;
    const pid_t owner = attributes->tid;
    attributes->tid = owner == 1 ? 2 : 1;
    during = true;
    assert(!normal_exit_fork_owner(&during) && !during);
    attributes->tid = owner;
    assert(normal_exit_fork_owner(&during) == owner && during);
    if (selected == Case::WorkerDuringTerminal) {
      assert(caller.cancellation.claim(false));
      assert(!normal_exit_fork_owner(&during) && !during);
      assert(caller.cancellation_suppression.fetch_add(1) == 0);
      bool single = true;
      assert(!M::thread_registry.freeze_for_fork(caller, 1, &single, true, M::ForkContinuation::NormalAfter));
      assert(!single);
      assert(caller.cancellation_suppression.fetch_sub(1) == 1);
      return;
    }
    if (selected == Case::WorkerDuringReturn) {
      // This callback was reached through the real worker cleanup branch.
      // Model its loader scope; no main phase is manually advanced here.
      assert(!caller.retains_resources_until_process_exit());
      assert(caller.loader_scopes.fetch_add(1) == 0);
      fork_worker_continuation(true);
      assert(worker_drain_returns == 0);
      assert(main_thread_cleanup_in_progress() ==
             caller.retains_resources_until_process_exit());
      assert(!main_thread_cleanup_finished());
      assert(caller.loader_scopes.fetch_sub(1) == 1);
      ++worker_drain_returns;
    }
    return;
  }
  assert(attributes == self.attrib && ++tls_drains == 1);
  assert(main_thread_cleanup_in_progress() && !main_thread_cleanup_finished());
  assert(!rebind_normal_exit_fork_owner(attributes->tid));
  assert(!adopt_fork_survivor_after_cleanup());
  assert(!adopt_fork_survivor_during_cleanup());
  if (selected == Case::Quick || selected == Case::CleanupOnly ||
      selected == Case::LastThreadReturn) {
    assert(!normal_exit_fork_owner());
    assert(!last_thread_exit_fork_owner());
    return;
  }

  auto &caller = *M::current_control;
  const pid_t owner = attributes->tid;
  assert(normal_exit_fork_owner() == owner && notifications_closed);
  // A process-callback marker cannot certify an unfinished TLS/TSS drain.
  normal_exit_phase = NormalExitPhase::ProcessCallbacks;
  assert(!normal_exit_fork_owner());
  normal_exit_phase = NormalExitPhase::ThreadCallbacks;

  M::ThreadControl worker;
  worker.attributes.tid = owner;
  worker.fork_recovery = ForkRecovery::child(true, true);
  M::current_control = &worker;
  self.attrib = &worker.attributes;
  assert(!main_thread_cleanup_in_progress() && !main_thread_cleanup_finished());
  assert(!normal_exit_fork_owner() && !rebind_normal_exit_fork_owner(owner));
  assert(!adopt_fork_survivor_during_cleanup());
  M::current_control = &caller;
  // Matching control alone cannot borrow main's attributes/phase.
  assert(!main_thread_cleanup_in_progress() && !normal_exit_fork_owner());
  assert(!adopt_fork_survivor_during_cleanup());
  self.attrib = attributes;
  assert(main_thread_cleanup_in_progress() &&
         normal_exit_fork_owner() == owner);

  if (selected == Case::TerminalDuring) {
    assert(caller.cancellation.claim(false));
    assert(!normal_exit_fork_owner());
    caller.fork_recovery = ForkRecovery::child(true, true);
    caller.attributes.tid = owner == 1 ? 2 : 1;
    assert(!rebind_normal_exit_fork_owner(owner));
    caller.attributes.tid = owner;
    return;
  }
  if (selected == Case::RecursiveTLSExit)
    exit_process(29);
  if (selected == Case::DuringReturn || selected == Case::RecursiveDuringChild)
    fork_during_cleanup();
}
[[noreturn]] void exit(int status) { throw ExitStatus{status}; }
} // namespace internal

extern "C" void mmix_normal_exit_test_finalize(void *dso) {
  assert(!dso && ++process_drains == 1 && tls_drains == 1);
  if (initial_during_case()) {
    auto &caller = *M::current_control;
    assert(drain_returns == 1 && after_roots == 1);
    assert(pending_tls == 1 && pending_tss == 1 && callback_returns == 1);
    assert(!M::initial_thread_during_fork_owner());
    assert(caller.attributes.retval.posix_retval == terminal_result());
    assert(caller.terminal_cleanup_finished() && I::main_thread_cleanup_finished());
    assert(notifications_closed && !I::normal_exit_fork_owner());
    fork_initial_continuation(false);
    return;
  }
  if (selected == Case::LastThreadReturn) {
    fork_after_last_thread_cleanup();
    return;
  }
  if (worker_case()) {
    auto &caller = *M::current_control;
    if (selected == Case::WorkerDuringReturn) {
      // Only the production worker-branch epilogue can complete this phase.
      assert(worker_drain_returns == 1 && !I::worker_draining);
      assert(caller.retains_resources_until_process_exit() == (during_forks > 0));
      assert(I::main_thread_cleanup_finished() == (during_forks > 0));
      assert(!I::main_thread_cleanup_in_progress());
      bool during = true;
      assert(I::normal_exit_fork_owner(&during) == caller.attributes.tid &&
             !during);
      assert(!caller.loader_scopes.load() && !caller.cancellation.enabled());
      fork_worker_continuation(false);
      assert(worker_drain_returns == 1 && tls_drains == 1 && main_drains == 1);
      return;
    }
    assert(!I::main_thread_cleanup_finished() &&
           !I::main_thread_cleanup_in_progress());
    // Still the parent worker, not a promoted child.
    assert(!I::adopt_fork_survivor_after_cleanup(true));
    if (selected == Case::WorkerTerminal ||
        selected == Case::WorkerDuringTerminal) {
      bool during = true;
      assert(!I::normal_exit_fork_owner(&during) && !during);
      return;
    }
    const pid_t owner = caller.attributes.tid;
    bool during = true;
    assert(I::normal_exit_fork_owner(&during) == owner && !during);
    I::normal_exit_phase = I::NormalExitPhase::ThreadCallbacks;
    assert(!I::normal_exit_fork_owner());
    I::normal_exit_phase = I::NormalExitPhase::ProcessCallbacks;
    M::ThreadControl other;
    other.attributes.tid = owner;
    M::current_control = &other;
    I::self.attrib = &other.attributes;
    assert(!I::normal_exit_fork_owner());
    M::current_control = &caller;
    assert(!I::normal_exit_fork_owner());
    I::self.attrib = &caller.attributes;
    assert(I::normal_exit_fork_owner() == owner);
    fork_after_worker_cleanup();
    return;
  }
  assert(I::main_thread_cleanup_finished() && notifications_closed);
  assert(!I::main_thread_cleanup_in_progress());
  assert(!I::adopt_fork_survivor_during_cleanup());
  auto &caller = *M::current_control;
  pid_t owner = caller.attributes.tid;
  if (selected == Case::TerminalDuring) {
    assert(!I::normal_exit_fork_owner());
    assert(!I::rebind_normal_exit_fork_owner(owner));
    return;
  }
  assert(I::normal_exit_fork_owner() == owner);
  I::normal_exit_phase = I::NormalExitPhase::ThreadCallbacks;
  assert(!I::normal_exit_fork_owner());
  I::normal_exit_phase = I::NormalExitPhase::ProcessCallbacks;
  if (selected == Case::DuringReturn ||
      selected == Case::RecursiveDuringChild) {
    assert(registrations == 1 + during_forks);
    assert(!I::rebind_normal_exit_fork_owner(owner));
    I::cleanup_current_thread();
    assert(tls_drains == 1 && process_drains == 1);
    assert(!M::thread_registry.claim_process_cleanup());
    return;
  }
  assert(!I::rebind_normal_exit_fork_owner(owner)); // Still the parent.
  assert(!I::adopt_fork_survivor_after_cleanup());
  if (selected == Case::RecursiveProcessExit)
    I::exit_process(31);
  if (selected == Case::Terminal) {
    assert(caller.cancellation.claim(false));
    assert(!I::normal_exit_fork_owner());
    assert(!I::rebind_normal_exit_fork_owner(owner));
    return;
  }

  // A worker must not borrow main's completed cleanup phase or owner marker.
  M::ThreadControl worker;
  worker.attributes.tid = owner;
  M::current_control = &worker;
  I::self.attrib = &worker.attributes;
  assert(!I::main_thread_cleanup_finished() && !I::normal_exit_fork_owner());
  M::current_control = &caller;
  I::self.attrib = &caller.attributes;

  pid_t child = ::fork();
  assert(child >= 0);
  if (!child) {
    ::alarm(10);
    caller.attributes.tid = ::getpid();
    assert(!I::normal_exit_fork_owner()); // Copied owner still names parent.
    assert(!I::rebind_normal_exit_fork_owner(owner)); // Not yet recovered.
    caller.fork_recovery = I::ForkRecovery::child();
    assert(!I::rebind_normal_exit_fork_owner(owner)); // ExecOnly is excluded.
    assert(!I::adopt_fork_survivor_after_cleanup());
    // Model only the private recovery capability. Actual registry restoration
    // and its admission certificate have separate lifecycle/guest tests.
    caller.fork_recovery = I::ForkRecovery::child(true, true);
    assert(!I::rebind_normal_exit_fork_owner(0));
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    assert(I::rebind_normal_exit_fork_owner(owner));
    assert(!I::rebind_normal_exit_fork_owner(owner)); // No second transfer.
    assert(I::normal_exit_fork_owner() == caller.attributes.tid);
    assert(I::adopt_fork_survivor_after_cleanup());
    assert(I::main_thread_cleanup_finished() && registrations == 2);
    I::cleanup_current_thread();
    assert(tls_drains == 1 && process_drains == 1);
    assert(!M::thread_registry.claim_process_cleanup());
    return; // Resume the copied exit_process; no recursive exit call.
  }
  wait_success(child);
  assert(caller.attributes.tid == owner &&
         I::normal_exit_fork_owner() == owner);
  assert(registrations == 1 && I::main_thread_cleanup_finished());
  assert(!M::thread_registry.claim_process_cleanup());
}
} // namespace LIBC_NAMESPACE_DECL

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long first,
                                         long second, long third, long fourth,
                                         long, long) {
  if (number == SYS_gettid) return M::current_control->attributes.tid;
  if (number == SYS_futex) {
    assert(second == FUTEX_WAKE_PRIVATE);
    return 0;
  }
  if (number == SYS_set_tid_address) {
    assert(first != 0 && M::current_control);
    ++registrations;
    if (registration_failure)
      return -1;
    return M::current_control->attributes.tid;
  }
  assert(number == SYS_rt_sigprocmask && first == SIG_SETMASK && fourth == 8);
  if (third)
    *reinterpret_cast<uint64_t *>(third) = mask;
  mask = *reinterpret_cast<const uint64_t *>(second);
  return 0;
}

static void exercise(Case test) {
  selected = test;
  M::ThreadControl caller;
  caller.attributes.tid = ::getpid();
  // activate_main_thread publishes current_control after the clear-TID call;
  // the injected kernel needs the planned TID for that first call too.
  M::current_control = &caller;
  assert(I::activate_main_thread(caller));
  assert(!I::main_thread_cleanup_finished() && !I::normal_exit_fork_owner());
  assert(!I::main_thread_cleanup_in_progress());
  assert(!I::adopt_fork_survivor_after_cleanup());
  assert(!I::adopt_fork_survivor_during_cleanup());
  if (initial_during_case()) {
    caller.attributes.style = LIBC_NAMESPACE::ThreadStyle::POSIX;
    caller.cancellation_identity = 19;
    caller.cancellation_generation = 23;
    // Inject the already-completed stack boundary, including the cancellation
    // claim. This does not execute target forced unwinding or claim to do so.
    if (cancellation_case()) {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(!lock.request_cancellation(caller, true));
      assert(lock.begin_cleanup(caller, true));
    } else M::begin_thread_cleanup(caller);
    caller.unwind_root.generation = 5;
    try {
      M::complete_thread_cleanup(caller, LIBC_NAMESPACE::ThreadReturnValue(terminal_result()));
    } catch (const ExitStatus &status) {
      assert(status.value == 0 && process_drains == 1 && tls_drains == 1);
      assert(drain_returns == 1 && after_roots == 1);
      assert(!M::initial_thread_during_fork_owner());
      assert(!I::last_thread_exit_fork_owner() && !I::normal_exit_fork_owner());
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.live_threads() == 0 && lock.process_exiting());
      assert(!lock.exit_ready(caller, 0) && !lock.claim_process_cleanup());
      return;
    }
    __builtin_trap();
  }
  if (test == Case::LastThreadReturn) {
    caller.cancellation.set_enabled(false);
    caller.cancellation.set_async(true);
    caller.cancellation_identity = 19;
    caller.cancellation_generation = 23;
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.begin_cleanup(caller));
    }
    I::cleanup_current_thread();
    assert(!caller.terminal_cleanup_finished()); // Still Cleaning, not AFTER.
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.close_leases(caller) && lock.exit_ready(caller, 41));
      assert(lock.termination_action(caller) == M::ThreadTermination::Process);
    }
    assert(caller.terminal_cleanup_finished());
    // Root capture/forced-unwind rejection is tested separately. Inject the
    // new root here to exercise real exit/lifecycle/reaper/adoption hooks.
    caller.unwind_root = {&caller, 8, 16, 24, 4, 6, true,
                          M::UnwindRootKind::LastThreadProcess};
    try {
      I::exit_after_thread_cleanup();
    } catch (const ExitStatus &status) {
      assert(status.value == 0 && tls_drains == 1 && process_drains == 1);
      assert(!I::last_thread_exit_fork_owner());
      assert(!I::normal_exit_fork_owner());
      check_terminal_child_state(caller);
      return;
    }
    __builtin_trap();
  }
  M::ThreadControl worker, helper;
  if (worker_case()) {
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.reserve() && lock.insert(worker, false) &&
             lock.start(worker));
      assert(lock.drop_creator(worker));
      if (test == Case::WorkerLiveReaper) {
        assert(lock.begin_helper_creation() && lock.insert(helper, true, true));
        assert(lock.start(helper) && lock.drop_creator(helper));
        M::bootstrap.attributes = &helper.attributes;
        M::bootstrap.control = &helper;
      }
      assert(lock.pin(&caller.attributes) == &caller &&
             lock.claim_join(caller));
      assert(lock.begin_cleanup(caller));
    }
    I::cleanup_current_thread();
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.close_leases(caller) && lock.exit_ready(caller, 0));
      caller.clear_tid.value.store(0);
      assert(lock.begin_reaping(caller) && lock.unpin(caller) &&
             lock.finish_reaping(caller));
    }
    static LIBC_NAMESPACE::ThreadAtExitCallbackMgr manager;
    worker.attributes.tid = ::getpid();
    worker.attributes.atexit_callback_mgr = &manager;
    worker.stack_mapping = {8192, 65536};
    worker.shared_tls.tp = 16384;
    worker.shared_tls.runtime_owner = 9;
    worker.unwind_root = {&worker, 1, 2, 3, 4, 5, true};
    worker.cancellation.set_enabled(false);
    worker.cancellation.set_async(true);
    M::current_control = &worker;
    I::self.attrib = &worker.attributes;
    assert(!worker.retains_resources_until_process_exit());
    if (test == Case::WorkerCleanupOnly) {
      I::cleanup_current_thread();
      bool during = true;
      assert(!I::normal_exit_fork_owner(&during) && !during);
      assert(!I::worker_draining && !I::worker_cleanup);
      assert(!I::main_thread_cleanup_in_progress() &&
             !I::main_thread_cleanup_finished());
      assert(main_drains == 1 && tls_drains == 1 && process_drains == 0 &&
             !notifications_closed);
      I::cleanup_current_thread();
      assert(tls_drains == 1);
      return;
    }
    if (test == Case::WorkerQuick) {
      I::begin_process_exit(0, true);
      I::cleanup_current_thread();
      assert(!I::normal_exit_fork_owner() && main_drains == 1 &&
             tls_drains == 1);
      return;
    }
    if (test == Case::WorkerTerminal) {
      {
        M::ThreadRegistryLock lock(M::thread_registry);
        assert(lock.begin_cleanup(worker));
      }
      I::cleanup_current_thread();
      worker.unwind_root.active = false;
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.close_leases(worker) && lock.exit_ready(worker, 0));
      assert(lock.claim_process_exit());
    }
    try {
      I::exit_process(37);
    } catch (const ExitStatus &status) {
      assert(status.value == 37 && main_drains == 1 && tls_drains == 1 &&
             process_drains == 1);
      assert(!I::normal_exit_fork_owner() && notifications_closed);
      assert(!I::worker_draining && !I::worker_cleanup);
      return;
    }
    __builtin_trap();
  }
  if (test == Case::CleanupOnly) {
    I::cleanup_current_thread();
    assert(I::main_thread_cleanup_finished());
    assert(!I::main_thread_cleanup_in_progress());
    assert(!I::normal_exit_fork_owner());
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    assert(tls_drains == 1 && process_drains == 0 && !notifications_closed);
    return;
  }
  if (test == Case::Quick) {
    I::begin_process_exit(0, true);
    I::cleanup_current_thread();
    assert(I::main_thread_cleanup_finished());
    assert(!I::normal_exit_fork_owner());
    assert(!I::rebind_normal_exit_fork_owner(caller.attributes.tid));
    return;
  }
  try {
    I::exit_process(23);
  } catch (const ExitStatus &status) {
    bool recursive_tls =
        test == Case::RecursiveTLSExit || recursive_during_child;
    int expected = recursive_tls                        ? 29
                   : test == Case::RecursiveProcessExit ? 31
                                                        : 23;
    assert(status.value == expected);
    assert(tls_drains == 1);
    assert(process_drains == (recursive_tls ? 0u : 1u));
    if (recursive_tls) {
      assert(I::main_thread_cleanup_in_progress());
      assert(!I::main_thread_cleanup_finished());
      assert(I::normal_exit_fork_owner() == caller.attributes.tid);
    }
    if (test == Case::Return || test == Case::Terminal ||
        test == Case::DuringReturn || test == Case::TerminalDuring ||
        (test == Case::RecursiveDuringChild && !recursive_during_child))
      assert(!I::normal_exit_fork_owner()); // Finalization scope has ended.
    return;
  }
  __builtin_trap();
}

int main() {
  const Case cases[] = {Case::Return,
                        Case::RecursiveProcessExit,
                        Case::RecursiveTLSExit,
                        Case::Terminal,
                        Case::Quick,
                        Case::DuringReturn,
                        Case::RecursiveDuringChild,
                        Case::TerminalDuring,
                        Case::CleanupOnly,
                        Case::WorkerReturn,
                        Case::WorkerDuringReturn,
                        Case::WorkerDuringTerminal,
                        Case::WorkerCleanupOnly,
                        Case::WorkerQuick,
                        Case::WorkerTerminal,
                        Case::WorkerLiveReaper,
                        Case::LastThreadReturn,
                        Case::InitialDuringTLS,
                        Case::InitialDuringTSS,
                        Case::InitialCancelTLS,
                        Case::InitialCancelTSS};
  for (Case test : cases) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      ::alarm(10);
      exercise(test);
      ::_exit(0);
    }
    wait_success(child);
  }
}
