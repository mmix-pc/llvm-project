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
struct Completed {};

static void retained_worker() {
  assert(worker.last_worker_cleanup_finished());
  assert(!worker.retains_resources_until_process_exit());
  assert(!worker.terminal_cleanup_finished() && !worker.terminal_cleanup_pending());
  assert(worker.control_mapping.base == 0x2000 && worker.control_mapping.size == 8192);
  assert(worker.stack_mapping.base == 0x4000 && worker.stack_mapping.size == 8192);
  assert(worker.tls.addr == 0x6000 && worker.tls.tp == 0x7000);
  assert(worker.owns_stack && !worker.resource_owner.context);
  assert(worker.cancellation.terminal() && !worker.cancellation.ready());
  assert(worker.cancellation_identity == 9 && worker.cancellation_generation == 7);
  assert(initial_callbacks == 1 && worker_callbacks == 1 && roots == 1);
}

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
bool has_c_cleanup() { return false; }
void run_with_unwind_root(ThreadControl &, RootBody *, void *) { __builtin_trap(); }
void run_with_terminal_cleanup_root(ThreadControl &, RootBody *, void *) {
  __builtin_trap(); // Worker DURING remains excluded.
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
    assert(lock.owner(worker) == M::ThreadOwner::Joinable && !lock.queued_reclaims());
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

int main(int argc, char **argv) {
  assert(argc == 2 && (argv[1][0] == '0' || argv[1][0] == '1'));
  bool cancel = argv[1][0] == '1';
  initial.attributes.tid = registered_tid = 1;
  assert(I::activate_main_thread(initial));
  worker.attributes.tid = 2;
  worker.attributes.atexit_callback_mgr =
      reinterpret_cast<LIBC_NAMESPACE::ThreadAtExitCallbackMgr *>(0x1800);
  worker.control_mapping = {0x2000, 8192};
  worker.stack_mapping = {0x4000, 8192};
  worker.tls = {8192, 0x6000, 0x7000};
  worker.owns_stack = true;
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.reserve() && lock.insert(worker, false) && lock.start(worker));
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
    return 0;
  }
  __builtin_trap();
}
