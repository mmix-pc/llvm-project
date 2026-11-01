//===-- Native MMIX last-worker completion and adoption -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production drain/finish/election/adoption, with injected callback, kernel,
// root and process-finalizer boundaries. Not target unwind/fork evidence.
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
static unsigned initial_callbacks, worker_callbacks, roots, registrations;
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

static void retained_worker() {
  assert(worker.last_worker_cleanup_finished());
  assert(!worker.retains_resources_until_process_exit());
  assert(!worker.terminal_cleanup_finished() && !worker.terminal_cleanup_pending());
  assert(worker.control_mapping.base == 0x2000 && worker.control_mapping.size == 8192);
  assert(worker.stack_mapping.base == 0x4000 && worker.stack_mapping.size == 8192);
  assert(worker.tls.addr == 0x6000 && worker.tls.tp == 0x7000);
  assert(worker.owns_stack);
  if (managed) {
    assert(worker.resource_owner.context == &worker);
    assert(worker.resource_owner.terminal_runner == managed_runner);
    assert(worker.resource_owner.retain == owner_hook &&
           worker.resource_owner.release == owner_hook);
    assert(worker.runner.posix_runner == managed_runner && worker.argument == &worker);
  } else {
    assert(!worker.resource_owner.context);
  }
  assert(worker.cancellation.terminal() && !worker.cancellation.ready());
  assert(worker.cancellation_identity == 9 && worker.cancellation_generation == 7);
  assert(initial_callbacks == 1 && worker_callbacks == 1 && roots == 1);
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(lock.callback_phase(worker) ==
         (mq ? M::CallbackPhase::Active : M::CallbackPhase::None));
  assert(lock.owner(worker) == (detached ? M::ThreadOwner::Detached
                                        : M::ThreadOwner::Joinable));
  assert(lock.live_threads() == 0 && lock.process_exiting());
  assert(lock.leases_drained(worker) && !lock.begin_reaping(worker));
  assert(worker.clear_tid.value.load() == UINT32_MAX);
  assert(lock.queued_reclaims() == unsigned(detached));
  if (detached) {
    assert(lock.take_reap_candidate() == &worker);
    assert(!lock.take_reap_candidate() && !lock.queued_reclaims());
    assert(lock.requeue_candidate(worker));
  }
}

static void detached_queue_admission(bool cancel) {
  enum Queue { Self, Missing, Foreign, ForeignThenSelf, SelfThenForeign };
  for (unsigned queue = Self; queue <= SelfThenForeign; ++queue) {
    M::ThreadRegistry process;
    M::ThreadControl main, caller, other;
    main.attributes.tid = 1;
    caller.attributes.atexit_callback_mgr =
        reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
    {
      M::ThreadRegistryLock lock(process);
      assert(lock.register_main(main));
      assert(lock.reserve() && lock.insert(caller, false) && lock.start(caller));
      assert(lock.drop_creator(caller));
      assert(lock.pin(&caller.attributes) == &caller && lock.detach(caller));
      assert(lock.unpin(caller) && !lock.queued_reclaims());
      assert(lock.begin_cleanup(main) && lock.close_leases(main) &&
             lock.exit_ready(main, 0));
      main.clear_tid.value.store(0);
      assert(lock.pin(&main.attributes) == &main && lock.claim_join(main));
      assert(lock.begin_reaping(main) && lock.unpin(main) && lock.finish_reaping(main));
      if (queue >= Foreign) {
        assert(lock.reserve() && lock.insert(other, true) && lock.start(other));
        assert(lock.drop_creator(other) && lock.begin_cleanup(other));
        assert(lock.close_leases(other) && lock.exit_ready(other, 0));
        other.clear_tid.value.store(0);
        // An outstanding reclaim, not a listed peer masking the queue guard.
        assert(lock.begin_reaping(other) && lock.requeue_candidate(other));
      }
      if (cancel) assert(!lock.request_cancellation(caller, true));
      assert(lock.begin_cleanup(caller, cancel) && caller.begin_callback_cleanup());
      assert(caller.record_worker_cleanup_finished());
      assert(lock.close_leases(caller) && lock.exit_ready(caller, 17));
      assert(lock.termination_action(caller) == M::ThreadTermination::Process);
      assert(caller.last_worker_cleanup_finished() && lock.claim_process_cleanup());
      if (queue == Missing) {
        assert(lock.take_reap_candidate() == &caller);
      } else if (queue == Foreign) {
        assert(lock.take_reap_candidate() == &other);
        assert(lock.take_reap_candidate() == &caller && lock.requeue_candidate(other));
      } else if (queue == SelfThenForeign) {
        assert(lock.take_reap_candidate() == &other && lock.requeue_candidate(other));
      }
    }
    caller.unwind_root = {&caller, 8, 16, 24, 4, 2, true,
                         M::UnwindRootKind::LastThreadProcess};
    caller.cancellation_suppression.store(1);
    bool single = true;
    bool frozen = process.freeze_for_fork(
        caller, 1, &single, true, M::ForkContinuation::LastWorkerAfter);
    assert(frozen == (queue == Self) && single == (queue == Self));
    if (frozen) process.release_after_fork();
    M::ThreadRegistryLock lock(process);
    assert(lock.owner(caller) == M::ThreadOwner::Detached && !lock.live_threads());
    assert(lock.leases_drained(caller) && !lock.begin_reaping(caller));
    assert(lock.queued_reclaims() == (queue == Missing ? 0u : queue <= Foreign ? 1u : 2u));
  }
}

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
bool has_c_cleanup() { return false; }
void run_with_unwind_root(ThreadControl &, RootBody *, void *) { __builtin_trap(); }
void run_with_terminal_cleanup_root(ThreadControl &control, RootBody *body,
                                     void *argument) {
  assert(&control == &worker && control.worker_cleanup_pending());
  assert(!control.unwind_root.active);
  control.unwind_root = {&control, 8, 16, 24, 4, 1, true,
                         UnwindRootKind::TerminalThreadCleanup};
  const auto root = control.unwind_root;
  body(argument);
  assert(root.matches(control.unwind_root));
  assert(!worker_thread_during_fork_owner());
  control.unwind_root.active = false;
}
[[noreturn]] void run_with_process_exit_root(ThreadControl &control,
                                           RootBody *body, void *argument) {
  assert(&control == &worker && !roots++ && !control.unwind_root.active);
  retained_worker();
  assert(!initial_thread_during_fork_owner());
  control.unwind_root = {&control, 8, 16, 24, 4, 2, true,
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
  } else {
    assert(attributes == &worker.attributes && !worker_callbacks++);
    assert(!worker.last_worker_cleanup_finished());
    if (managed) {
      assert(worker.worker_cleanup_pending());
      assert(worker.worker_cleanup_in_progress());
    }
    assert(!main_thread_cleanup_finished() && !main_thread_cleanup_in_progress());
    assert(!adopt_fork_survivor_after_worker_exit());
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.live_threads() == 1 && !lock.process_exiting());
  }
}
[[noreturn]] void exit_process(int) { __builtin_trap(); }
[[noreturn]] void exit_after_thread_cleanup() {
  retained_worker();
  assert(M::thread_registry.claim_process_cleanup());
  assert(!adopt_fork_survivor_after_worker_exit()); // Not a fork child yet.
  const auto root = worker.unwind_root;
  worker.cancellation_suppression.store(1);
  constexpr auto kind = M::ForkContinuation::LastWorkerAfter;
  bool single = false;
  assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, false, kind));
  assert(!single);
  constexpr M::ForkContinuation wrong_kinds[] = {
      M::ForkContinuation::LastThreadAfter,
      M::ForkContinuation::InitialThreadDuring,
      M::ForkContinuation::WorkerThreadDuring};
  for (auto wrong : wrong_kinds) {
    single = true;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, wrong));
    assert(!single);
  }
  auto *manager = worker.attributes.atexit_callback_mgr;
  worker.attributes.atexit_callback_mgr = nullptr;
  assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
  assert(!single);
  worker.attributes.atexit_callback_mgr = manager;
  worker.unwind_root.kind = M::UnwindRootKind::Thread;
  assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
  assert(!single);
  worker.unwind_root = root;
  worker.resource_owner = {&worker, +[](void *) -> long { return 0; },
                          +[](void *) -> long { return 0; }};
  assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
  assert(!single);
  worker.resource_owner = {};
  if (managed) {
    if (mq) {
      assert(!worker.last_worker_cleanup_finished());
      assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    }
    worker.resource_owner = managed_owner();
    worker.resource_owner.terminal_runner = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.resource_owner = managed_owner();
    worker.argument = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.argument = &worker;
    worker.attributes.style = ThreadStyle::STDC;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.attributes.style = ThreadStyle::POSIX;
    worker.runner.posix_runner = nullptr;
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.runner.posix_runner = managed_runner;
    worker.resource_owner.context = nullptr;
    assert(!worker.resource_owner.valid());
    assert(!M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind));
    worker.resource_owner = managed_owner();
  }
  retained_worker();
  assert(M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind) && single);
  M::thread_registry.release_after_fork(); // Failed clone leaves the owner intact.
  retained_worker();
  for (unsigned child = 0; child != 2; ++child) {
    assert(M::thread_registry.freeze_for_fork(worker, 1, &single, true, kind) && single);
    ++worker.attributes.tid;
    registered_tid = worker.attributes.tid;
    M::thread_registry.restore_fork_child(worker, ForkRecovery::child(true, true), kind);
    assert(!adopt_fork_survivor_after_thread_exit());
    self.attrib = &initial.attributes;
    assert(!adopt_fork_survivor_after_worker_exit());
    self.attrib = &worker.attributes;
    assert(adopt_fork_survivor_after_worker_exit());
    retained_worker();
    assert(root.matches(worker.unwind_root));
    assert(!main_thread_cleanup_finished() && !main_thread_cleanup_in_progress());
    // The retired initial thread still owns its old Finished phase. Worker
    // adoption must not replace active_attributes, even in a terminal child.
    M::current_control = &initial;
    self.attrib = &initial.attributes;
    assert(main_thread_cleanup_finished());
    M::current_control = &worker;
    self.attrib = &worker.attributes;
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.live_threads() == 0 && lock.process_exiting());
    assert(!lock.reserve() && !lock.begin_helper_creation());
    assert(!lock.pin(&initial.attributes));
    assert(lock.pin(&worker.attributes) == &worker);
    assert(!lock.acquire_lease(worker) && lock.unpin(worker));
    assert(lock.owner(worker) == (detached ? M::ThreadOwner::Detached
                                          : M::ThreadOwner::Joinable));
    assert(lock.queued_reclaims() == unsigned(detached));
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
  if (detached) detached_queue_admission(cancel);
  initial.attributes.tid = registered_tid = 1;
  assert(I::activate_main_thread(initial));
  worker.attributes.tid = 2;
  worker.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
  worker.control_mapping = {0x2000, 8192};
  worker.stack_mapping = {0x4000, 8192};
  worker.tls = {8192, 0x6000, 0x7000};
  worker.owns_stack = true;
  if (managed) {
    worker.resource_owner = managed_owner();
    worker.runner.posix_runner = managed_runner;
    worker.argument = &worker;
    assert(!worker.worker_cleanup_pending());
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
  if (cancel) {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(!lock.request_cancellation(worker, true));
    assert(lock.begin_cleanup(worker, true));
  } else {
    M::begin_thread_cleanup(worker);
  }
  // Inject the completed-stack handoff, not a public forced-unwind execution.
  try {
    M::complete_thread_cleanup(worker, LIBC_NAMESPACE::ThreadReturnValue(&worker));
  } catch (Completed &) {
    retained_worker();
    assert(registrations == 3);
    return;
  }
  __builtin_trap();
}

int main(int argc, char **argv) {
  assert(argc == 2 && (argv[1][0] == '0' || argv[1][0] == '1'));
  // Host isolation preserves production's one-shot registry/initial phase.
  // These subprocesses are not MMIX fork-continuation evidence.
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
