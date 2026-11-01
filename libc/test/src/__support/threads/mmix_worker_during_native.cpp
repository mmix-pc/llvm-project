//===-- Native MMIX original-worker terminal drain continuation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production drain/registry/adoption with injected kernel, root and callback
// boundaries. This is not target fork, TLS/TSS or forced-unwind evidence.
#include <cassert>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define LIBC_MMIX_UNWIND_ROOTS
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/main_thread.cpp"
#include "src/__support/threads/linux/mmix/thread_finish.cpp"
#include "src/__support/threads/linux/mmix/thread_process_exit.cpp"
#include "src/errno/libc_errno.cpp"

namespace M = LIBC_NAMESPACE::mmix;
namespace I = LIBC_NAMESPACE::internal;
static M::ThreadControl initial, worker;
static unsigned initial_callbacks, worker_callbacks, drain_roots, after_roots;
static unsigned registrations, callback_progress;
static uint64_t signal_mask = 0x1234;
static int registered_tid;
static bool detached;
static bool managed;
static bool mq;
static void *managed_runner(void *) { __builtin_trap(); }
static long owner_hook(void *) { __builtin_trap(); }
static M::ThreadResourceOwner managed_owner() {
  return {&worker, owner_hook, owner_hook, managed_runner};
}
struct Completed {};

static void backing() {
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.callback_phase(worker) ==
           (mq ? M::CallbackPhase::Active : M::CallbackPhase::None));
  }
  assert(!worker.retains_resources_until_process_exit());
  assert(!worker.terminal_cleanup_pending() && !worker.terminal_cleanup_finished());
  assert(worker.control_mapping.base == 0x2000 && worker.control_mapping.size == 8192);
  assert(worker.stack_mapping.base == 0x4000 && worker.stack_mapping.size == 8192);
  assert(worker.stack_bottom == 0x4000 && worker.stack_top == 0x6000);
  assert(worker.tls.addr == 0x6000 && worker.tls.tp == 0x7000 && worker.tls.size == 8192);
  assert(worker.owns_stack && worker.resource_owner.valid());
  if (managed) {
    assert(worker.resource_owner.context == &worker);
    assert(worker.resource_owner.retain == owner_hook &&
           worker.resource_owner.release == owner_hook);
    assert(worker.resource_owner.terminal_runner == managed_runner);
    assert(worker.runner.posix_runner == managed_runner && worker.argument == &worker);
  } else {
    assert(!worker.resource_owner.context);
  }
  assert(worker.cancellation.terminal() && !worker.cancellation.ready());
  assert(worker.cancellation_identity == 9 && worker.cancellation_generation == 7);
  assert(initial_callbacks == 1 && worker_callbacks == 1 && drain_roots == 1);
  assert(!I::main_thread_cleanup_finished() && !I::main_thread_cleanup_in_progress());
  M::current_control = &initial;
  I::self.attrib = &initial.attributes;
  assert(I::main_thread_cleanup_finished());
  M::current_control = &worker;
  I::self.attrib = &worker.attributes;
}

static void during() {
  backing();
  assert(worker.worker_cleanup_in_progress() && !worker.last_worker_cleanup_finished());
  assert(M::worker_thread_during_fork_owner() == worker.attributes.tid);
  assert(!M::initial_thread_during_fork_owner() && !after_roots);
  assert(M::thread_registry.admit_signal_notification());
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(lock.live_threads() == 1 && !lock.process_exiting());
  assert(lock.owner(worker) == (detached ? M::ThreadOwner::Detached
                                        : M::ThreadOwner::Joinable));
  assert(!lock.queued_reclaims());
  assert(!lock.begin_reaping(worker) && !lock.pin(&initial.attributes));
  assert(lock.pin(&worker.attributes) == &worker);
  assert(lock.acquire_lease(worker) && lock.release_lease(worker) && lock.unpin(worker));
  assert(!lock.exit_ready(worker, 99)); // Leases must still be open.
  // termination_action requires ExitReady; the DURING certificate must not
  // publish that state merely to inspect the eventual election.
}

static void copied_drain() {
  constexpr auto kind = M::ForkContinuation::WorkerThreadDuring;
  const auto root = worker.unwind_root;
  auto *manager = worker.attributes.atexit_callback_mgr;
  assert(!I::adopt_fork_survivor_during_worker_exit());
  assert(!M::rebind_worker_thread_during_fork_owner(worker.attributes.tid));
  worker.attributes.atexit_callback_mgr = nullptr;
  assert(!M::worker_thread_during_fork_owner());
  worker.attributes.atexit_callback_mgr = manager;
  ++worker.unwind_root.generation;
  assert(!M::worker_thread_during_fork_owner());
  worker.unwind_root = root;
  I::self.attrib = &initial.attributes;
  assert(!M::worker_thread_during_fork_owner());
  I::self.attrib = &worker.attributes;
  worker.cancellation_suppression.store(1);
  bool single = false;
  constexpr M::ForkContinuation rejected_kinds[] = {
      M::ForkContinuation::InitialThreadDuring,
      M::ForkContinuation::LastWorkerAfter};
  for (auto rejected : rejected_kinds) {
    single = true;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, rejected));
    assert(!single);
  }
  if (managed) {
    if (mq) {
      worker.resource_owner = {};
      assert(!worker.worker_cleanup_pending());
      assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
      worker.resource_owner = managed_owner();
    }
    worker.resource_owner.terminal_runner = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.resource_owner = managed_owner();
    worker.argument = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.argument = &worker;
    worker.attributes.style = LIBC_NAMESPACE::ThreadStyle::STDC;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.attributes.style = LIBC_NAMESPACE::ThreadStyle::POSIX;
    worker.runner.posix_runner = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.runner.posix_runner = managed_runner;
    worker.resource_owner.context = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.resource_owner = managed_owner();
  }
  assert(M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind) && single);
  M::thread_registry.release_after_fork(); // No-child rollback, no adoption/rebind.
  assert(root.matches(worker.unwind_root));
  assert(worker.attributes.atexit_callback_mgr == manager && callback_progress == 1);
  assert(!worker.fork_recovery.is_child() && registrations == 1);
  during();
  for (unsigned child = 0; child != 2; ++child) {
    const auto previous = worker.attributes.tid;
    assert(M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind) && single);
    registered_tid = ++worker.attributes.tid;
    M::thread_registry.restore_fork_child(worker, I::ForkRecovery::child(true, true), kind);
    assert(!M::worker_thread_during_fork_owner()); // Copied owner TID is stale.
    assert(!M::rebind_worker_thread_during_fork_owner(previous + 10));
    assert(!M::rebind_initial_thread_during_fork_owner(previous));
    assert(!I::adopt_fork_survivor_during_thread_exit());
    assert(!I::adopt_fork_survivor_after_worker_exit());
    assert(I::adopt_fork_survivor_during_worker_exit());
    assert(registrations == child + 2);
    assert(worker.clear_tid.value.load() == UINT32_MAX);
    assert(M::rebind_worker_thread_during_fork_owner(previous));
    assert(!M::rebind_worker_thread_during_fork_owner(previous));
    assert(root.matches(worker.unwind_root));
    assert(worker.attributes.atexit_callback_mgr == manager && callback_progress == 1);
    during();
  }
  worker.cancellation_suppression.store(0);
}

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
bool has_c_cleanup() { return false; }
void run_with_unwind_root(ThreadControl &, RootBody *, void *) { __builtin_trap(); }
void run_with_terminal_cleanup_root(ThreadControl &control, RootBody *body,
                                     void *argument) {
  assert(&control == &worker && !drain_roots++ && !control.unwind_root.active);
  assert(control.worker_cleanup_pending() && !control.worker_cleanup_in_progress());
  control.unwind_root = {&control, 8, 16, 24, 4, 2, true,
                         UnwindRootKind::TerminalThreadCleanup};
  const auto root = control.unwind_root;
  assert(!worker_thread_during_fork_owner());
  body(argument);
  assert(root.matches(control.unwind_root) && callback_progress == 4);
  assert(!worker_thread_during_fork_owner()); // Certificate withdrawn first.
  assert(control.worker_cleanup_in_progress() && !control.last_worker_cleanup_finished());
  control.unwind_root.active = false;
}
[[noreturn]] void run_with_process_exit_root(ThreadControl &control,
                                           RootBody *body, void *argument) {
  assert(&control == &worker && !after_roots++ && !control.unwind_root.active);
  backing();
  assert(control.last_worker_cleanup_finished() && !control.worker_cleanup_pending());
  assert(!worker_thread_during_fork_owner() && !initial_thread_during_fork_owner());
  control.unwind_root = {&control, 8, 16, 24, 4, 3, true,
                         UnwindRootKind::LastThreadProcess};
  body(argument);
  __builtin_trap();
}
bool initialize_credentials(ThreadControl &control) {
  assert(&control == &initial);
  return true;
}
CredentialGate::CredentialGate() { __builtin_trap(); }
CredentialGate::~CredentialGate() { __builtin_trap(); }
void unregister_credentials(ThreadControl &) { __builtin_trap(); }
} // namespace mmix
namespace internal {
ThreadAtExitCallbackMgr *get_thread_atexit_callback_mgr() {
  return reinterpret_cast<ThreadAtExitCallbackMgr *>(0x1000);
}
void call_atexit_callbacks(ThreadAttributes *attributes) {
  assert(attributes == self.attrib);
  if (attributes == &initial.attributes) {
    assert(!initial_callbacks++);
    assert(!M::worker_thread_during_fork_owner());
    return;
  }
  assert(attributes == &worker.attributes && !worker_callbacks++);
  during();
  ++callback_progress; // Inject a returning first TLS callback.
  copied_drain();
  during();
  ++callback_progress; // Pending TLS work, then two TSS iterations.
  during();
  ++callback_progress;
  during();
  ++callback_progress;
}
[[noreturn]] void exit_process(int) { __builtin_trap(); }
[[noreturn]] void exit_after_thread_cleanup() {
  backing();
  assert(after_roots == 1 && callback_progress == 4 && worker.last_worker_cleanup_finished());
  assert(registrations == 3);
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.live_threads() == 0 && lock.process_exiting());
    assert(lock.owner(worker) == (detached ? M::ThreadOwner::Detached
                                          : M::ThreadOwner::Joinable));
    assert(lock.queued_reclaims() == unsigned(detached));
    assert(lock.leases_drained(worker) && !lock.begin_reaping(worker));
  }
  assert(M::thread_registry.claim_process_cleanup());
  constexpr auto kind = M::ForkContinuation::LastWorkerAfter;
  const auto root = worker.unwind_root;
  worker.cancellation_suppression.store(1);
  bool single = false;
  assert(M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind) && single);
  registered_tid = ++worker.attributes.tid;
  M::thread_registry.restore_fork_child(worker, ForkRecovery::child(true, true), kind);
  assert(!adopt_fork_survivor_during_worker_exit());
  assert(!M::rebind_worker_thread_during_fork_owner(worker.attributes.tid - 1));
  assert(adopt_fork_survivor_after_worker_exit());
  assert(registrations == 4);
  assert(root.matches(worker.unwind_root));
  backing();
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.live_threads() == 0 && lock.process_exiting());
    assert(lock.owner(worker) == (detached ? M::ThreadOwner::Detached
                                          : M::ThreadOwner::Joinable));
    assert(lock.queued_reclaims() == unsigned(detached));
    if (detached) {
      assert(lock.take_reap_candidate() == &worker && !lock.take_reap_candidate());
      assert(!lock.queued_reclaims());
      assert(lock.requeue_candidate(worker) && lock.queued_reclaims() == 1);
    }
    assert(lock.pin(&worker.attributes) == &worker);
    assert(!lock.acquire_lease(worker) && lock.unpin(worker));
    assert(!lock.reserve() && !lock.begin_helper_creation());
    assert(!lock.begin_reaping(worker) && !lock.claim_process_cleanup());
    assert(worker.attributes.retval.posix_retval == &worker);
  }
  worker.cancellation_suppression.store(0);
  throw Completed{};
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

extern "C" [[noreturn]] void __llvm_libc_mmix_thread_exit() { __builtin_trap(); }
extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long a, long b,
                                         long c, long d, long, long) {
  if (number == SYS_rt_sigprocmask) {
    assert(a == SIG_SETMASK && d == sizeof(signal_mask));
    if (c) *reinterpret_cast<uint64_t *>(c) = signal_mask;
    signal_mask = *reinterpret_cast<uint64_t *>(b);
    return 0;
  }
  if (number == SYS_set_tid_address) {
    auto &control = registrations++ ? worker : initial;
    assert(a == reinterpret_cast<long>(&control.clear_tid.value.val));
    return registered_tid;
  }
  assert(number == SYS_futex && b == FUTEX_WAKE_PRIVATE);
  return 0;
}

static void run_case(bool cancel) {
  initial.attributes.tid = registered_tid = 1;
  assert(I::activate_main_thread(initial));
  worker.attributes.tid = 2;
  worker.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
  worker.control_mapping = {0x2000, 8192};
  worker.stack_mapping = {0x4000, 8192};
  worker.stack_bottom = 0x4000;
  worker.stack_top = 0x6000;
  worker.tls = {8192, 0x6000, 0x7000};
  worker.owns_stack = true;
  if (managed) {
    worker.resource_owner = managed_owner();
    worker.runner.posix_runner = managed_runner;
    worker.argument = &worker;
  }
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.reserve());
    if (mq) {
      assert(lock.insert_callback_waiter(worker) && lock.start(worker));
      assert(lock.callback_phase(worker) == M::CallbackPhase::Waiting);
      assert(lock.activate_callback(worker));
      worker.cancellation.set_enabled(true);
    } else {
      assert(lock.insert(worker, false) && lock.start(worker));
    }
    assert(lock.drop_creator(worker) && lock.begin_cleanup(initial));
  }
  I::cleanup_current_thread();
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.close_leases(initial) && lock.exit_ready(initial, 0));
    initial.clear_tid.value.store(0);
    assert(lock.pin(&initial.attributes) == &initial && lock.claim_join(initial));
    assert(lock.begin_reaping(initial) && lock.unpin(initial) && lock.finish_reaping(initial));
  }
  M::current_control = &worker;
  I::self.attrib = &worker.attributes;
  if (detached && !mq) {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.pin(&worker.attributes) == &worker && lock.detach(worker));
    assert(lock.unpin(worker) && !lock.queued_reclaims());
  }
  worker.cancellation_identity = 9;
  worker.cancellation_generation = 7;
  assert(!M::worker_thread_during_fork_owner());
  if (cancel) {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(!lock.request_cancellation(worker, true) && lock.begin_cleanup(worker, true));
  } else {
    M::begin_thread_cleanup(worker);
  }
  assert(!M::worker_thread_during_fork_owner());
  // Inject only the completed-stack handoff; no public forced-unwind claim.
  try {
    M::complete_thread_cleanup(worker, LIBC_NAMESPACE::ThreadReturnValue(&worker));
  } catch (Completed &) {
    backing();
    // Initial registration, two DURING adoptions, then one AFTER adoption.
    assert(registrations == 4);
    assert(callback_progress == 4 && after_roots == 1);
    return;
  }
  __builtin_trap();
}

int main(int argc, char **argv) {
  assert(argc == 2 && (argv[1][0] == '0' || argv[1][0] == '1'));
  // Each ownership case starts with untouched production registry/phase state.
  for (unsigned owner = 0; owner != 4; ++owner) {
    pid_t child = ::fork();
    assert(child >= 0);
    if (!child) {
      detached = owner != 0;
      managed = owner >= 2;
      mq = owner == 3;
      run_case(argv[1][0] == '1');
      ::_exit(0);
    }
    int status;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
}
