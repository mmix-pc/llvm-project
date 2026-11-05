//===-- MMIX Linux main-thread lifecycle ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "main_thread.h"
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
#include "credentials.h"
#endif
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/thread.h"
#include <sys/syscall.h>
#ifdef LIBC_MMIX_ROBUST_MUTEX
#include "robust_mutex.h"
#endif

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE &&                             \
    (LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM ||                          \
     LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL)
#error "MMIX Linux TLS main-thread state requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control = nullptr;
}
#endif
namespace internal {
namespace {

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE
ThreadAttributes main_thread_attributes;
#endif
ThreadAttributes *active_attributes = nullptr;
enum class Phase { Uninitialized, Active, Cleaning, Finished };
Phase phase = Phase::Uninitialized;

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
enum class RobustInheritance { Preserve, Discard };

bool register_fork_child(mmix::ThreadControl &control,
                         [[maybe_unused]] RobustInheritance robust) {
  control.clear_tid.value.store(UINT32_MAX);
#ifdef LIBC_MMIX_ROBUST_MUTEX
  int error = robust == RobustInheritance::Discard
                  ? mmix::reset_robust_list_after_fork()
                  : mmix::register_robust_list(true);
  if (error)
    return false;
#endif
  return syscall_impl(SYS_set_tid_address,
                      reinterpret_cast<long>(&control.clear_tid.value.val)) ==
         control.attributes.tid;
}
#endif

} // namespace

bool initialize_main_thread() {
  if (phase != Phase::Uninitialized)
    return phase == Phase::Active;

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE
  long tid = syscall_impl(SYS_gettid);
  if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
    return false;

  main_thread_attributes.tid = static_cast<int>(tid);
  main_thread_attributes.atexit_callback_mgr = get_thread_atexit_callback_mgr();
  self.attrib = &main_thread_attributes;
  active_attributes = &main_thread_attributes;
  phase = Phase::Active;
  return true;
#else
  // TLS startup must activate prepared state before lifecycle callbacks.
  return false;
#endif
}

bool restore_minimal_fork_child() {
  if (!self.attrib || self.attrib->tid <= 0)
    return false;
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes)
    return false;
  // The child may have interrupted any internal critical section. Preserve
  // copied locks/owners, but never deliver a parent's cancellation request or
  // enter its credential rendezvous. Only safe operations precede exec/_exit.
  control->fork_recovery = ForkRecovery::child();
  control->unwind_root.active = false;
  control->cancellation.restore_after_fork();
  control->cancellation_identity = control->cancellation_generation = 0;
  // Interrupted safe syscalls and trusted signal entries still own live stack
  // registrations. Keep their LIFO publication until they return; the cleared
  // identity/root prevents these inherited descriptors from delivering cancel.
  control->cancellation_transfer.store(0);
  control->credential_pending.store(0);
  return register_fork_child(*control, RobustInheritance::Discard);
#else
  return true;
#endif
}

bool main_thread_cleanup_finished() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  // Do not inspect main's mutable phase from an ordinary worker.
  return control && control->retains_resources_until_process_exit() &&
         self.attrib == &control->attributes &&
         self.attrib == active_attributes && phase == Phase::Finished;
#else
  return false;
#endif
}

bool main_thread_cleanup_in_progress() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  // Do not inspect main's mutable phase from an ordinary worker.
  return control && control->retains_resources_until_process_exit() &&
         self.attrib == &control->attributes &&
         self.attrib == active_attributes && phase == Phase::Cleaning;
#else
  return false;
#endif
}

bool adopt_fork_survivor_after_thread_exit() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || !main_thread_cleanup_finished() ||
      !control->terminal_cleanup_finished() ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork())
    return false;
  return register_fork_child(*control, RobustInheritance::Preserve);
#else
  return false;
#endif
}

bool adopt_terminal_exec_only_survivor() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->terminal_exec_only_survivor())
    return false;
  active_attributes = self.attrib;
  phase = Phase::Finished;
  return register_fork_child(*control, RobustInheritance::Discard);
#else
  return false;
#endif
}

bool adopt_quick_exit_fork_survivor() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork() || !control->quick_cleanup_running() ||
      control->cancellation_suppression.load() != 2)
    return false;
  // Resume the copied quick drain. Do not start or finish TLS/TSS cleanup.
  return register_fork_child(*control, RobustInheritance::Preserve);
#else
  return false;
#endif
}

bool adopt_active_cleanup_fork_survivor() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork())
    return false;
  const auto &root = control->unwind_root;
  if (!root.active || root.owner != control ||
      root.kind != mmix::UnwindRootKind::Thread || !root.generation ||
      !root.chain)
    return false;
  // Pending without in-progress proves Cleaning/terminal before callbacks.
  // Only the original process-lifetime carrier may inspect main's phase.
  if (control->retains_resources_until_process_exit()) {
    if (!control->terminal_cleanup_pending() ||
        control->terminal_cleanup_in_progress() ||
        self.attrib != active_attributes || phase != Phase::Active)
      return false;
  } else if (!control->worker_cleanup_pending() ||
             control->worker_cleanup_in_progress()) {
    return false;
  }
  return register_fork_child(*control, RobustInheritance::Preserve);
#else
  return false;
#endif
}

bool adopt_active_cleanup_exec_only_survivor() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->fork_recovery.is_child() ||
      control->fork_recovery.allows_fork() || !control->cancellation.terminal() ||
      (!control->terminal_cleanup_pending() &&
       !control->worker_cleanup_pending()) ||
      control->terminal_cleanup_in_progress() ||
      control->worker_cleanup_in_progress() ||
      control->unwind_root.owner != control || !control->unwind_root.active ||
      control->unwind_root.kind != mmix::UnwindRootKind::Thread ||
      !control->unwind_root.chain || !control->unwind_root.generation)
    return false;
  // Do not expose inherited robust registrations to child kernel exit. The
  // copied cleanup/manager/backing remain untouched and may not resume.
  return register_fork_child(*control, RobustInheritance::Discard);
#else
  return false;
#endif
}

bool adopt_fork_survivor_during_thread_exit() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || !main_thread_cleanup_in_progress() ||
      !control->terminal_cleanup_in_progress() ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork())
    return false;
  return register_fork_child(*control, RobustInheritance::Preserve);
#else
  return false;
#endif
}

bool adopt_fork_survivor_after_worker_exit() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->last_worker_cleanup_finished() ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork())
    return false;
  return register_fork_child(*control, RobustInheritance::Preserve);
#else
  return false;
#endif
}

bool adopt_fork_survivor_during_worker_exit() {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->worker_cleanup_in_progress() ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork())
    return false;
  return register_fork_child(*control, RobustInheritance::Preserve);
#else
  return false;
#endif
}

bool adopt_fork_survivor_after_cleanup([[maybe_unused]] bool worker_cleanup) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->retains_resources_until_process_exit() ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork() || control->cancellation.terminal() ||
      (!worker_cleanup && !main_thread_cleanup_finished()))
    return false;
  if (!register_fork_child(*control, RobustInheritance::Preserve))
    return false;
  if (worker_cleanup) {
    active_attributes = self.attrib;
    phase = Phase::Finished;
  }
  return true;
#else
  return false;
#endif
}

bool adopt_fork_survivor_during_cleanup([[maybe_unused]] bool worker_cleanup) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->retains_resources_until_process_exit() ||
      !control->fork_recovery.is_child() ||
      !control->fork_recovery.allows_fork() || control->cancellation.terminal() ||
      (!worker_cleanup && !main_thread_cleanup_in_progress()))
    return false;
  if (!register_fork_child(*control, RobustInheritance::Preserve))
    return false;
  if (worker_cleanup) {
    active_attributes = self.attrib;
    phase = Phase::Cleaning;
  }
  return true;
#else
  return false;
#endif
}

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
bool restore_main_thread_after_fork() {
  auto *control = mmix::current_control;
  if (phase != Phase::Active || !control || self.attrib != active_attributes ||
      self.attrib != &control->attributes ||
      !control->retains_resources_until_process_exit())
    return false;
  // No peer, reaper or creation reservation has ever existed. All pointers and
  // unlocked runtime objects remain valid in the copied address space.
  return register_fork_child(*control, RobustInheritance::Preserve);
}

bool adopt_fork_survivor() {
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes ||
      !control->fork_recovery.is_child())
    return false;
  active_attributes = self.attrib;
  phase = Phase::Active;
  return register_fork_child(*control, RobustInheritance::Preserve);
}

bool activate_main_thread(MainThreadState &state) {
  if (phase != Phase::Uninitialized || self.attrib || state.attributes.tid <= 0)
    return false;
#ifdef LIBC_MMIX_ROBUST_MUTEX
  if (mmix::register_robust_list())
    return false;
#endif
  self.attrib = &state.attributes;
  libc_errno = 0;
  auto *manager = get_thread_atexit_callback_mgr();
  if (!manager ||
      syscall_impl(SYS_set_tid_address,
                   reinterpret_cast<long>(&state.clear_tid.value.val)) !=
          state.attributes.tid) {
    self.attrib = nullptr;
    return false;
  }
  state.attributes.atexit_callback_mgr = manager;
  {
    // Startup has not installed user handlers or published any worker. Do not
    // run callback setup or the clear-TID syscall while holding this lock.
    mmix::ThreadRegistryLock lock(mmix::thread_registry);
    if (!lock.register_main(state)) {
      self.attrib = nullptr;
      return false;
    }
  }
  active_attributes = &state.attributes;
  mmix::current_control = &state;
  if (!mmix::initialize_credentials(state))
    return false;
  phase = Phase::Active;
  return true;
}
#endif

namespace {
enum class CleanupKind { ThreadExit, ProcessExit };

void drain_callbacks(ThreadAttributes *attributes, CleanupKind kind) {
  if (kind == CleanupKind::ProcessExit)
    call_tls_atexit_callbacks(attributes);
  else
    call_atexit_callbacks(attributes);
}

void cleanup_main_thread(CleanupKind kind) {
  if (phase != Phase::Active)
    return;
  phase = Phase::Cleaning;
  drain_callbacks(active_attributes, kind);
  phase = Phase::Finished;
}

void cleanup_current_thread(CleanupKind kind) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE
  cleanup_main_thread(kind);
#else
  if (!self.attrib)
    return;
  auto *control = mmix::current_control;
  if (!control || self.attrib != &control->attributes)
    __builtin_trap();
  if (!control->begin_callback_cleanup())
    return;
  // Only main may access the main-thread lifecycle phase and attributes.
  if (control->retains_resources_until_process_exit())
    cleanup_main_thread(kind);
  else {
    drain_callbacks(self.attrib, kind);
    // A fork inside this worker drain can promote only its child. Complete
    // that copied activation after its selected drain, without draining again.
    if (control->retains_resources_until_process_exit()) {
      if (mmix::current_control != control ||
          !control->fork_recovery.is_child() ||
          !control->fork_recovery.allows_fork() ||
          !main_thread_cleanup_in_progress())
        __builtin_trap();
      phase = Phase::Finished;
    }
  }
#endif
}

} // namespace

void cleanup_main_thread() { cleanup_main_thread(CleanupKind::ThreadExit); }

void cleanup_current_thread() { cleanup_current_thread(CleanupKind::ThreadExit); }

void cleanup_current_thread_for_process_exit() {
  cleanup_current_thread(CleanupKind::ProcessExit);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
