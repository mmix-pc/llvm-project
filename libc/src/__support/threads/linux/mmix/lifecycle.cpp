//===-- MMIX Linux thread lifetime primitives
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/linux/mmix/lifecycle.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

LIBC_CONSTINIT ThreadRegistry thread_registry;

bool ThreadRegistry::quiescent_fork_caller(ThreadControl &caller,
                                           ForkContinuation kind) {
  if (!mutex.value.try_lock())
    return false;
  if (kind == ForkContinuation::LastThreadAfter ||
      kind == ForkContinuation::LastWorkerAfter ||
      kind == ForkContinuation::InitialThreadDuring ||
      kind == ForkContinuation::WorkerThreadDuring) {
    const bool during = kind == ForkContinuation::InitialThreadDuring ||
                        kind == ForkContinuation::WorkerThreadDuring;
    const bool quiescent =
        during ? terminal_during_fork_caller(caller, kind,
                                             ForkSnapshot::Recoverable)
               : terminal_fork_caller(caller, kind);
    mutex.value.unlock();
    return quiescent;
  }
  const bool ordinary = kind == ForkContinuation::Ordinary &&
                        !process_exiting() && !caller.callbacks_started &&
                        !caller.cancellation.terminal();
  const bool normal_cleanup =
      (kind == ForkContinuation::NormalAfter ||
       kind == ForkContinuation::NormalDuring) && caller.callbacks_started &&
      process_exit_state.load(cpp::MemoryOrder::ACQUIRE) == CLEANUP_CLAIMED &&
      !caller.cancellation.terminal() && !caller.internal_helper &&
      caller.callback_phase == CallbackPhase::None;
  bool quiescent =
      (ordinary || normal_cleanup ||
       (kind == ForkContinuation::QuickCallback &&
        quick_cleanup_fork_caller(caller))) && main_registered && !process_operation &&
      fork_recovery.allows_fork() && caller.fork_recovery.allows_fork() &&
      !reservations && !helper_reservations && !unreaped_helpers &&
      running_callback_census(caller) && live == 1 && head == &caller && !caller.next &&
      !reap_head && !reap_tail && !reap_count && caller.registry == this &&
      caller.listed && caller.execution == ThreadExecution::Running;
  mutex.value.unlock();
  return quiescent;
}

bool ThreadRegistry::terminal_fork_caller(ThreadControl &caller,
                                          ForkContinuation kind,
                                          ForkSnapshot snapshot) {
  const bool restricted = snapshot == ForkSnapshot::TerminalExecOnly;
  const auto &root = caller.unwind_root;
  const bool completed = kind == ForkContinuation::LastWorkerAfter
                             ? caller.last_worker_cleanup_finished()
                             : caller.terminal_cleanup_finished();
  const bool owner_queue =
      (caller.owner == ThreadOwner::Joinable && !caller.queued &&
       !caller.reap_next &&
       (restricted || (!reap_head && !reap_tail && !reap_count))) ||
      (((kind == ForkContinuation::LastThreadAfter &&
         caller.process_lifetime) ||
        (kind == ForkContinuation::LastWorkerAfter && !caller.initial_thread &&
         !caller.process_lifetime)) &&
       caller.owner == ThreadOwner::Detached &&
       caller.queued &&
       (restricted || (!caller.reap_next && reap_head == &caller &&
                       reap_tail == &caller && reap_count == 1)));
  // Resumable AFTER retains only its own completed queue entry, with no live
  // reclaimer. Restricted children instead preserve all backing until exec.
  return main_registered && fork_recovery.allows_fork() &&
         caller.fork_recovery.allows_fork() && !process_operation &&
         process_exit_state.load(cpp::MemoryOrder::ACQUIRE) == CLEANUP_CLAIMED &&
         !reservations && !helper_reservations &&
         (restricted || (!unreaped_helpers && terminal_callback_census(caller))) &&
         !live && head == &caller &&
         !caller.next &&
         owner_queue && caller.registry == this &&
         caller.listed && !caller.counted && caller.lifecycle_pin &&
         !caller.creator_pin && !caller.api_pins && !caller.leases_open &&
         !caller.leases && !caller.internal_helper && completed && root.active &&
         root.kind == UnwindRootKind::LastThreadProcess &&
         root.owner == &caller && root.generation && root.chain;
}

bool ThreadRegistry::terminal_during_fork_caller(ThreadControl &caller,
                                                ForkContinuation kind,
                                                ForkSnapshot snapshot) {
  if (kind != ForkContinuation::InitialThreadDuring &&
      kind != ForkContinuation::WorkerThreadDuring)
    return false;
  const auto &root = caller.unwind_root;
  const bool draining = kind == ForkContinuation::WorkerThreadDuring
                            ? caller.worker_cleanup_in_progress()
                            : caller.terminal_cleanup_in_progress();
  // A detached carrier is not queued until ExitReady. A certified original
  // worker, like the initial carrier, still owns its backing during this drain.
  const bool owner =
      caller.owner == ThreadOwner::Joinable ||
      (caller.owner == ThreadOwner::Detached &&
       ((kind == ForkContinuation::InitialThreadDuring &&
         caller.process_lifetime) ||
        (kind == ForkContinuation::WorkerThreadDuring && !caller.initial_thread &&
         !caller.process_lifetime)));
  // A restricted child never resumes this drain or reclaims copied owners.
  // Foreign live threads, callback slots and reap entries therefore need not
  // be quiescent; the caller's own terminal activation must still be intact.
  const bool isolated = !unreaped_helpers && terminal_callback_census(caller) &&
                        live == 1 && head == &caller && !caller.next &&
                        !reap_head && !reap_tail && !reap_count;
  return main_registered && fork_recovery.allows_fork() &&
         caller.fork_recovery.allows_fork() && !process_operation &&
         process_exit_state.load(cpp::MemoryOrder::ACQUIRE) == RUNNING &&
         !reservations && !helper_reservations &&
         (snapshot == ForkSnapshot::TerminalExecOnly || isolated) &&
         caller.registry == this &&
         caller.listed && caller.counted && caller.lifecycle_pin &&
         !caller.creator_pin && !caller.api_pins && caller.leases_open &&
         !caller.leases && !caller.queued && !caller.reap_next &&
         owner && !caller.internal_helper && draining && root.active &&
         root.kind == UnwindRootKind::TerminalThreadCleanup &&
         root.owner == &caller && root.generation && root.chain;
}

bool ThreadRegistry::normal_cleanup_fork_caller(ThreadControl &caller,
                                               ForkContinuation kind) {
  return (kind == ForkContinuation::NormalDuring ||
          kind == ForkContinuation::NormalAfter) &&
         main_registered && fork_recovery.allows_fork() &&
         caller.fork_recovery.allows_fork() && !process_operation &&
         !reservations && !helper_reservations && caller.registry == this &&
         caller.listed && caller.execution == ThreadExecution::Running &&
         caller.callbacks_started && !caller.cancellation.terminal() &&
         !caller.internal_helper && caller.callback_phase == CallbackPhase::None &&
         process_exit_state.load(cpp::MemoryOrder::ACQUIRE) == CLEANUP_CLAIMED;
}

bool ThreadRegistry::quick_cleanup_fork_caller(ThreadControl &caller) {
  return main_registered && fork_recovery.allows_fork() &&
         caller.fork_recovery.allows_fork() && !process_operation &&
         !reservations && !helper_reservations && caller.registry == this &&
         caller.listed && caller.counted && caller.lifecycle_pin &&
         caller.execution == ThreadExecution::Running &&
         !caller.callbacks_started && !caller.cancellation.terminal() &&
         !caller.internal_helper &&
         process_exit_state.load(cpp::MemoryOrder::ACQUIRE) == CLEANUP_CLAIMED;
}

bool ThreadRegistry::freeze_for_fork(ThreadControl &caller,
                                     uint64_t suppression, bool *single_thread,
                                     bool helper_drained,
                                     ForkContinuation kind,
                                     ForkSnapshot snapshot) {
  if (single_thread)
    *single_thread = false;
  const bool exec_only = snapshot == ForkSnapshot::TerminalExecOnly;
  const bool quick = kind == ForkContinuation::QuickCallback;
  // The caller already owns the credential gate and other snapshot locks.
  // An interrupted owner can need that gate in an async-safe setter handler.
  if (!mutex.value.try_lock())
    return false;
  const bool during = kind == ForkContinuation::InitialThreadDuring ||
                      kind == ForkContinuation::WorkerThreadDuring;
  const bool after = kind == ForkContinuation::LastThreadAfter ||
                     kind == ForkContinuation::LastWorkerAfter;
  const bool normal = kind == ForkContinuation::NormalDuring ||
                      kind == ForkContinuation::NormalAfter;
  if ((snapshot != ForkSnapshot::Recoverable && !exec_only) ||
      (quick && suppression != 2) ||
      (exec_only && ((!during && !after && !normal && !quick) ||
                     suppression != (quick ? 2U : 1U)))) {
    mutex.value.unlock();
    return false;
  }
  if (kind == ForkContinuation::LastThreadAfter ||
      kind == ForkContinuation::LastWorkerAfter ||
      kind == ForkContinuation::InitialThreadDuring ||
      kind == ForkContinuation::WorkerThreadDuring) {
    const bool eligible =
        during ? terminal_during_fork_caller(caller, kind, snapshot)
               : terminal_fork_caller(caller, kind, snapshot);
    if ((!exec_only && !helper_drained) || !eligible ||
        caller.cancellation_transfer.load() ||
        caller.cancellation_suppression.load() != suppression ||
        caller.cancellation_point.load()) {
      mutex.value.unlock();
      return false;
    }
    if (single_thread)
      *single_thread = !exec_only;
    return true;
  }
  const bool process_cleanup = kind != ForkContinuation::Ordinary;
  // The private exit-owner certificate is checked by ForkOperation. Keep its
  // claim and certified callback continuation rather than restarting cleanup
  // merely to pass admission.
  bool continuation = quick ? quick_cleanup_fork_caller(caller)
                            : normal_cleanup_fork_caller(caller, kind);
  if ((process_cleanup && !continuation) ||
      (process_exiting() && !continuation) || reservations ||
      helper_reservations || process_operation || caller.registry != this ||
      !caller.listed || caller.execution != ThreadExecution::Running ||
      (caller.callbacks_started && !continuation) ||
      !caller.fork_recovery.allows_fork() ||
      caller.cancellation_transfer.load() ||
      caller.cancellation_suppression.load() != suppression ||
      caller.cancellation_point.load()) {
    mutex.value.unlock();
    return false;
  }
  // The census is necessary, but resource release after final reap must still
  // be witnessed independently by the caller through helper_drained.
  if (single_thread)
    *single_thread = !exec_only && (!creation_started || helper_drained) &&
                     !unreaped_helpers && running_callback_census(caller) &&
                     live == 1 && head == &caller &&
                     !caller.next && !reap_head && !reap_tail && !reap_count;
  return true;
}

void ThreadRegistry::release_after_fork() { mutex.value.unlock(); }

bool ThreadRegistry::active_cleanup_spawn_caller(ThreadControl &caller) {
  if (!mutex.value.try_lock())
    return false;
  const auto &root = caller.unwind_root;
  const bool admitted =
      main_registered && fork_recovery.allows_fork() &&
      caller.fork_recovery.allows_fork() && !process_operation &&
      !process_exiting() && caller.registry == this && caller.listed &&
      caller.counted && caller.lifecycle_pin && !caller.internal_helper &&
      caller.execution == ThreadExecution::Cleaning &&
      !caller.callbacks_started &&
      caller.leases_open && !caller.queued && !caller.reap_next &&
      caller.cancellation.terminal() && !caller.cancellation_transfer.load() &&
      !caller.cancellation_point.load() &&
      caller.cancellation_suppression.load() == 1 && root.active &&
      root.owner == &caller && root.kind == UnwindRootKind::Thread &&
      root.chain && root.generation;
  mutex.value.unlock();
  return admitted;
}

bool ThreadRegistry::active_cleanup_fork_caller(ThreadControl &caller,
                                                ForkSnapshot snapshot) {
  const auto &root = caller.unwind_root;
  const bool restricted = snapshot == ForkSnapshot::TerminalExecOnly;
  const bool isolated = !unreaped_helpers && terminal_callback_census(caller) &&
                        live == 1 && head == &caller && !caller.next &&
                        !reap_head && !reap_tail && !reap_count;
  return main_registered && fork_recovery.allows_fork() &&
         caller.fork_recovery.allows_fork() && !process_operation &&
         !process_exiting() && !reservations && !helper_reservations &&
         (restricted || isolated) &&
         caller.registry == this &&
         caller.listed && caller.counted && caller.lifecycle_pin &&
         !caller.creator_pin && (restricted || !caller.api_pins) &&
         caller.leases_open && (restricted || !caller.leases) &&
         !caller.queued && !caller.reap_next &&
         !caller.internal_helper &&
         (caller.callback_phase == CallbackPhase::None ||
          caller.retained_active_callback()) &&
         (caller.owner == ThreadOwner::Joinable ||
          caller.owner == ThreadOwner::Detached ||
          (restricted && caller.owner == ThreadOwner::JoinOwner)) &&
         caller.execution == ThreadExecution::Cleaning &&
         !caller.callbacks_started && caller.cancellation.terminal() &&
         !caller.cancellation_transfer.load() &&
         !caller.cancellation_point.load() &&
         caller.cancellation_suppression.load() == 1 && root.active &&
         root.owner == &caller && root.kind == UnwindRootKind::Thread &&
         root.generation && root.chain && root.ro && root.sp && root.procedure;
}

bool ThreadRegistry::freeze_active_cleanup_for_fork(ThreadControl &caller,
                                                   ForkSnapshot snapshot) {
  if (!mutex.value.try_lock())
    return false;
  if (active_cleanup_fork_caller(caller, snapshot))
    return true;
  mutex.value.unlock();
  return false;
}

void ThreadRegistry::restore_active_cleanup_fork_child(
    ThreadControl &caller, internal::ForkRecovery recovery,
    ForkSnapshot snapshot) {
  if (!recovery.is_child() ||
      recovery.allows_fork() != (snapshot == ForkSnapshot::Recoverable) ||
      !active_cleanup_fork_caller(caller, snapshot))
    __builtin_trap();
  // Never traverse copied peer owners, refund references or normalize phase.
  // Restricted children retain the whole snapshot inert until exec/_exit.
  // In a sole snapshot a retained managed callback's
  // own slot and runner still belong to the copied activation. Its normal
  // unwind/drain/reap path, not fork restoration, releases them.
  fork_recovery = caller.fork_recovery = recovery;
  event.value.store(0);
  RawMutex::init(&mutex.value);
}

void ThreadRegistry::restore_fork_child(ThreadControl &survivor,
                                        internal::ForkRecovery recovery,
                                        ForkContinuation kind,
                                        ForkSnapshot snapshot) {
  if (!recovery.is_child())
    __builtin_trap();
  const bool quick = kind == ForkContinuation::QuickCallback;
  if (snapshot == ForkSnapshot::TerminalExecOnly) {
    const bool during = kind == ForkContinuation::InitialThreadDuring ||
                        kind == ForkContinuation::WorkerThreadDuring;
    const bool after = kind == ForkContinuation::LastThreadAfter ||
                       kind == ForkContinuation::LastWorkerAfter;
    const bool eligible =
        during ? terminal_during_fork_caller(survivor, kind, snapshot)
        : after ? terminal_fork_caller(survivor, kind, snapshot)
        : quick ? quick_cleanup_fork_caller(survivor)
                : normal_cleanup_fork_caller(survivor, kind);
    if (recovery.allows_fork() ||
        !eligible ||
        survivor.cancellation_suppression.load() != (quick ? 2U : 1U) ||
        survivor.cancellation_transfer.load() ||
        survivor.cancellation_point.load())
      __builtin_trap();
    // Only the copied registry is discarded. Backing and invocation owners
    // remain mapped, without release hooks, until this child execs or _exits.
    head = &survivor;
    reap_head = reap_tail = nullptr;
    reap_count = reservations = unreaped_callbacks = 0;
    helper_reservations = unreaped_helpers = 0;
    live = 1;
    process_exit_state.store(RUNNING, cpp::MemoryOrder::RELAXED);
    process_operation = false;
    main_registered = creation_started = true;
    fork_recovery = recovery;
    survivor.registry = this;
    survivor.next = survivor.reap_next = nullptr;
    survivor.queued = survivor.creator_pin = survivor.leases_open = false;
    survivor.listed = survivor.counted = survivor.lifecycle_pin = true;
    survivor.process_lifetime = true;
    survivor.internal_helper = false;
    survivor.callback_phase = CallbackPhase::None;
    survivor.callbacks_started = true;
    survivor.completed_worker_callback_mgr = nullptr;
    survivor.last_worker_exit_elected = false;
    survivor.api_pins = survivor.leases = 0;
    survivor.owner = ThreadOwner::Joinable;
    survivor.execution = ThreadExecution::Running;
    survivor.fork_recovery = recovery;
    survivor.unwind_root.active = false;
    survivor.cancellation.restore_after_fork();
    survivor.cancellation_identity = survivor.cancellation_generation = 0;
    survivor.cancellation_point.store(nullptr);
    survivor.cancellation_transfer.store(0);
    // Retain the operation's level and, for quick callbacks, the drain's
    // permanent level. Neither may be mistaken for permission to cancel.
    event.value.store(0);
    RawMutex::init(&mutex.value);
    return;
  }
  if (snapshot != ForkSnapshot::Recoverable)
    __builtin_trap();
  if (kind == ForkContinuation::LastThreadAfter ||
      kind == ForkContinuation::LastWorkerAfter ||
      kind == ForkContinuation::InitialThreadDuring ||
      kind == ForkContinuation::WorkerThreadDuring) {
    const bool during = kind == ForkContinuation::InitialThreadDuring ||
                        kind == ForkContinuation::WorkerThreadDuring;
    const bool eligible =
        during ? terminal_during_fork_caller(survivor, kind, snapshot)
               : terminal_fork_caller(survivor, kind);
    if (!recovery.allows_fork() || !eligible)
      __builtin_trap();
    // The frozen registry already contains exactly this terminal task. Do not
    // recount it, change leases/election, erase cleanup or release its backing.
    fork_recovery = survivor.fork_recovery = recovery;
    event.value.store(0);
    RawMutex::init(&mutex.value);
    return;
  }
  const bool process_cleanup = kind != ForkContinuation::Ordinary;
  if (quick && (!recovery.allows_fork() ||
                !quick_cleanup_fork_caller(survivor) ||
                survivor.cancellation_suppression.load() != 2))
    __builtin_trap();
  if (process_cleanup && !quick &&
      (!recovery.allows_fork() || !survivor.callbacks_started ||
       survivor.cancellation.terminal() || survivor.internal_helper ||
       survivor.callback_phase != CallbackPhase::None ||
       survivor.execution != ThreadExecution::Running ||
       process_exit_state.load() != CLEANUP_CLAIMED))
    __builtin_trap();
  // Only the survivor exists in this copied address space. Do not traverse or
  // reclaim vanished stacks, copied pins, leases or reaper queue entries.
  head = &survivor;
  reap_head = reap_tail = nullptr;
  reap_count = reservations = unreaped_callbacks = 0;
  helper_reservations = unreaped_helpers = 0;
  live = 1;
  process_exit_state.store(process_cleanup ? CLEANUP_CLAIMED : RUNNING,
                           cpp::MemoryOrder::RELAXED);
  process_operation = false;
  main_registered = true;
  // Only a proven quiescent snapshot starts a fresh creation history.
  // reserve/begin_helper_creation set it even if later preparation fails.
  creation_started = !recovery.allows_thread_creation();
  fork_recovery = recovery;
  survivor.registry = this;
  survivor.next = survivor.reap_next = nullptr;
  survivor.queued = survivor.creator_pin = false;
  survivor.leases_open = recovery.allows_thread_creation();
  survivor.listed = survivor.counted = survivor.lifecycle_pin = true;
  survivor.process_lifetime = true;
  survivor.internal_helper = false;
  survivor.callbacks_started = process_cleanup && !quick;
  // The survivor's invocation backing is now process-lifetime, not a detached
  // callback awaiting reaping. Keep resource_owner and the suspended runner;
  // neither the copied service nor registry may refund that backing early.
  survivor.callback_phase = CallbackPhase::None;
  survivor.api_pins = survivor.leases = 0;
  survivor.owner = ThreadOwner::Joinable;
  survivor.execution = ThreadExecution::Running;
  survivor.fork_recovery = recovery;
  event.value.store(0);
  RawMutex::init(&mutex.value);
}

ThreadRegistryLock::ThreadRegistryLock(ThreadRegistry &r) : registry(r) {
  registry.mutex.value.lock();
}

ThreadRegistryLock::~ThreadRegistryLock() {
  if (changed)
    registry.event.value.fetch_add(1, cpp::MemoryOrder::RELEASE);
  registry.mutex.value.unlock();
  // A last-pin release can make a control freeable immediately after unlock.
  // Only process-lifetime memory may be touched here.
  if (changed)
    registry.event.value.notify_all();
}

bool ThreadRegistryLock::belongs(const ThreadControl &c) const {
  return c.registry == &registry && c.lifecycle_pin;
}

uint32_t ThreadRegistryLock::sequence() {
  return registry.event.value.load(cpp::MemoryOrder::ACQUIRE);
}

bool ThreadRegistryLock::reserve() {
  if (registry.process_exiting() || registry.process_operation ||
      !registry.fork_recovery.allows_thread_creation() ||
      registry.reservations == SIZE_MAX)
    return false;
  registry.creation_started = true;
  ++registry.reservations;
  change();
  return true;
}

bool ThreadRegistryLock::begin_helper_creation() {
  if (registry.process_exiting() || registry.process_operation ||
      !registry.fork_recovery.allows_thread_creation() ||
      registry.helper_reservations == SIZE_MAX)
    return false;
  registry.creation_started = true;
  ++registry.helper_reservations;
  change();
  return true;
}

bool ThreadRegistryLock::sole_fork_caller(const ThreadControl &caller) const {
  // Reaper retirement uses this predicate while that helper still exists.
  // Pending helpers must settle first; final admission checks unreaped helpers.
  // Allow the certified callback's own slot, not ordinary child recovery.
  const bool callback = caller.retained_active_callback();
  return registry.main_registered && !registry.process_exiting() &&
         !registry.process_operation && !registry.reservations &&
         !registry.helper_reservations &&
         registry.terminal_callback_census(caller) &&
         (!callback || (belongs(caller) && caller.listed && caller.counted &&
                        !caller.creator_pin && !caller.api_pins &&
                        caller.leases_open && !caller.leases &&
                        !caller.queued && !caller.reap_next)) &&
         registry.live == 1 && registry.head == &caller && !caller.next &&
         !registry.reap_head && !registry.reap_tail && !registry.reap_count &&
         caller.execution == ThreadExecution::Running &&
         !caller.callbacks_started && caller.fork_recovery.allows_fork();
}

bool ThreadRegistryLock::begin_process_operation(const ThreadControl &caller) {
  // Never reopen admission after a failed creation or retirement: a helper or
  // other runtime state may survive even when application accounting is one.
  if (!registry.main_registered || registry.creation_started ||
      registry.fork_recovery.is_child() ||
      registry.process_operation || registry.process_exiting() ||
      registry.live != 1 || registry.reservations || registry.head != &caller ||
      caller.execution != ThreadExecution::Running || caller.callbacks_started)
    return false;
  registry.process_operation = true;
  return true;
}

bool ThreadRegistryLock::end_process_operation() {
  if (!registry.process_operation)
    return false;
  registry.process_operation = false;
  return true;
}

bool ThreadRegistryLock::begin_vfork(const ThreadControl &caller) {
  // The caller has retired the reaper. Preserve creation history, but freeze
  // new reservations until the shared-VM child has execed or exited.
  if (!sole_fork_caller(caller) || registry.unreaped_helpers ||
      !caller.fork_recovery.allows_thread_creation())
    return false;
  registry.process_operation = true;
  return true;
}

bool ThreadRegistryLock::cancel_reservation() {
  if (!registry.reservations)
    return false;
  --registry.reservations;
  change();
  return true;
}

bool ThreadRegistryLock::cancel_helper_reservation() {
  if (!registry.helper_reservations)
    return false;
  --registry.helper_reservations;
  change();
  return true;
}

bool ThreadRegistryLock::insert(ThreadControl &c, bool detached, bool helper) {
  if (c.registry || c.owner != ThreadOwner::Unpublished || !c.creator_pin ||
      !c.lifecycle_pin || registry.process_exiting() ||
      registry.process_operation ||
      (helper ? (!registry.helper_reservations ||
                 registry.unreaped_helpers == SIZE_MAX)
              : (!registry.reservations || registry.live == SIZE_MAX)))
    return false;
  c.registry = &registry;
  registry.creation_started = true;
  c.owner = detached ? ThreadOwner::Detached : ThreadOwner::Joinable;
  c.internal_helper = helper;
  if (helper)
    c.cancellation.set_enabled(false);
  c.listed = !helper;
  c.leases_open = !helper;
  c.counted = !helper;
  if (!helper) {
    c.next = registry.head;
    registry.head = &c;
  }
  if (helper) {
    --registry.helper_reservations;
    ++registry.unreaped_helpers;
  } else {
    --registry.reservations;
    ++registry.live;
  }
  change();
  return true;
}

bool ThreadRegistryLock::insert_callback_waiter(ThreadControl &c) {
  if (registry.unreaped_callbacks == SIZE_MAX ||
      c.callback_phase != CallbackPhase::None || !insert(c, true))
    return false;
  // insert's ordinary reservation transfer is not externally visible until
  // this lock is released. Only activation may publish the callback handle.
  registry.head = c.next;
  c.next = nullptr;
  c.listed = c.leases_open = c.counted = false;
  --registry.live;
  ++registry.unreaped_callbacks;
  c.callback_phase = CallbackPhase::Waiting;
  c.cancellation.set_enabled(false);
  return true;
}

bool ThreadRegistryLock::activate_callback(ThreadControl &c) {
  if (!belongs(c) || c.callback_phase != CallbackPhase::Waiting ||
      c.execution != ThreadExecution::Running || registry.process_exiting() ||
      registry.process_operation || registry.live == SIZE_MAX ||
      !registry.fork_recovery.allows_thread_creation())
    return false;
  c.callback_phase = CallbackPhase::Active;
  ++registry.live;
  c.listed = c.leases_open = c.counted = true;
  c.next = registry.head;
  registry.head = &c;
  // The callback thread restores cancellation on its own ordinary chain after
  // observing activation. Requests may accumulate while it is still disabled.
  change();
  return true;
}

bool ThreadRegistryLock::discard_callback(ThreadControl &c) {
  if (!belongs(c) || c.callback_phase != CallbackPhase::Waiting ||
      c.execution != ThreadExecution::Running)
    return false;
  c.callback_phase = CallbackPhase::Discarded;
  change();
  return true;
}

bool ThreadRegistryLock::register_main(ThreadControl &c) {
  if (registry.main_registered || registry.process_exiting() || registry.head ||
      registry.live || registry.reservations || registry.unreaped_callbacks ||
      registry.helper_reservations || registry.unreaped_helpers ||
      c.registry ||
      c.owner != ThreadOwner::Unpublished || !c.creator_pin ||
      !c.lifecycle_pin || c.attributes.tid <= 0 || c.owns_stack ||
      c.attributes.owned_stack || c.control_mapping.size || c.stack_mapping.size)
    return false;
  c.registry = &registry;
  c.owner = ThreadOwner::Joinable;
  c.execution = ThreadExecution::Running;
  c.creator_pin = false;
  c.listed = c.leases_open = c.counted = c.process_lifetime = true;
  c.initial_thread = true;
  registry.head = &c;
  registry.live = 1;
  registry.main_registered = true;
  change();
  return true;
}

bool ThreadRegistryLock::adopt_abort(ThreadControl &c) {
  if (c.registry || c.owner != ThreadOwner::Unpublished || !c.creator_pin ||
      !c.lifecycle_pin)
    return false;
  c.registry = &registry;
  c.owner = ThreadOwner::AbortOwner;
  change();
  return true;
}

ThreadControl *ThreadRegistryLock::pin(ThreadAttributes *handle) {
  for (ThreadControl *c = registry.head; c; c = c->next) {
    if (&c->attributes != handle)
      continue;
    if (c->api_pins == SIZE_MAX)
      return nullptr;
    ++c->api_pins;
    return c;
  }
  return nullptr;
}

bool ThreadRegistryLock::unpin(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins)
    return false;
  if (--c.api_pins == 0)
    change();
  return true;
}

bool ThreadRegistryLock::drop_creator(ThreadControl &c) {
  if (!belongs(c) || !c.creator_pin)
    return false;
  c.creator_pin = false;
  change();
  return true;
}

bool ThreadRegistryLock::claim_join(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins || c.owner != ThreadOwner::Joinable)
    return false;
  c.owner = ThreadOwner::JoinOwner;
  change();
  return true;
}

bool ThreadRegistryLock::release_join(ThreadControl &c) {
  if (!belongs(c) || c.owner != ThreadOwner::JoinOwner)
    return false;
  c.owner = ThreadOwner::Joinable;
  change();
  return true;
}

bool ThreadRegistryLock::join_result(ThreadControl &c,
                                    ThreadReturnValue &result) const {
  if (!belongs(c) || c.owner != ThreadOwner::JoinOwner || !c.api_pins ||
      c.execution != ThreadExecution::ExitReady || c.leases_open || c.leases ||
      c.clear_tid.value.load(cpp::MemoryOrder::ACQUIRE) != 0)
    return false;
  if (c.result_kind == ThreadResultKind::Pointer ||
      c.result_kind == ThreadResultKind::Canceled)
    result = ThreadReturnValue(c.attributes.retval.posix_retval);
  else if (c.result_kind == ThreadResultKind::Integer)
    result = ThreadReturnValue(c.attributes.retval.stdc_retval);
  else
    return false;
  return true;
}

bool ThreadRegistryLock::detach(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins || c.owner != ThreadOwner::Joinable)
    return false;
  c.owner = ThreadOwner::Detached;
  enqueue_completed(c);
  change();
  return true;
}

bool ThreadRegistryLock::start(ThreadControl &c) {
  if (!belongs(c) || (!c.listed && !c.internal_helper &&
                     c.callback_phase != CallbackPhase::Waiting) ||
      c.execution != ThreadExecution::Starting)
    return false;
  c.execution = ThreadExecution::Running;
  return true;
}

bool ThreadRegistry::begin_cleanup_locked(ThreadControl &c, bool cancellation) {
  if (c.registry != this || !c.lifecycle_pin ||
      c.execution != ThreadExecution::Running ||
      c.callback_phase == CallbackPhase::Waiting ||
      !c.cancellation.claim(cancellation))
    return false;
  c.execution = ThreadExecution::Cleaning;
  return true;
}

bool ThreadRegistryLock::begin_cleanup(ThreadControl &c, bool cancellation) {
  return registry.begin_cleanup_locked(c, cancellation);
}

bool ThreadRegistry::try_signal_cleanup_impl(ThreadControl &c) {
  if (!mutex.value.try_lock())
    return false;
  const bool claimed = begin_cleanup_locked(c, true);
  mutex.value.unlock();
  return claimed;
}

int ThreadRegistryLock::request_cancellation(ThreadControl &c, bool owner) {
  if (!belongs(c) || (!owner && (!c.api_pins || !c.leases)) ||
      c.internal_helper || c.callback_phase == CallbackPhase::Waiting ||
      c.callback_phase == CallbackPhase::Discarded ||
      (c.execution != ThreadExecution::Starting &&
       c.execution != ThreadExecution::Running) || c.cancellation.terminal())
    return ESRCH;
  if (registry.process_operation)
    return EBUSY;
  return c.cancellation.request() ? 0 : ESRCH;
}

bool ThreadRegistryLock::exit_ready(ThreadControl &c,
                                    ThreadReturnValue result) {
  if (!belongs(c) || c.execution != ThreadExecution::Cleaning ||
      c.leases_open || c.leases)
    return false;
  c.attributes.retval = result;
  if (c.result_kind == ThreadResultKind::Unpublished)
    c.result_kind = c.attributes.style == ThreadStyle::STDC
                        ? ThreadResultKind::Integer
                        : ThreadResultKind::Pointer;
  c.execution = ThreadExecution::ExitReady;
  enqueue_completed(c);
  if (c.counted) {
    --registry.live;
    c.counted = false;
  }
  change();
  return true;
}

bool ThreadRegistryLock::acquire_lease(ThreadControl &c) {
  if (!belongs(c) || !c.api_pins || !c.leases_open || !c.listed ||
      c.leases == SIZE_MAX)
    return false;
  ++c.leases;
  return true;
}

bool ThreadRegistryLock::release_lease(ThreadControl &c) {
  if (!belongs(c) || !c.leases)
    return false;
  if (--c.leases == 0)
    change();
  return true;
}

bool ThreadRegistryLock::close_leases(ThreadControl &c) {
  if (!belongs(c))
    return false;
  c.leases_open = false;
  change();
  return true;
}

bool ThreadRegistryLock::begin_reaping(ThreadControl &c) {
  if (!belongs(c) || c.leases_open || c.leases ||
      c.clear_tid.value.load(cpp::MemoryOrder::ACQUIRE) != 0)
    return false;
  if (c.owner != ThreadOwner::AbortOwner &&
      ((c.owner != ThreadOwner::JoinOwner &&
        c.owner != ThreadOwner::Detached) ||
       c.execution != ThreadExecution::ExitReady))
    return false;
  remove_queued(c);
  if (c.listed) {
    ThreadControl **link = &registry.head;
    while (*link != &c)
      link = &(*link)->next;
    *link = c.next;
    c.next = nullptr;
    c.listed = false;
  }
  c.owner = ThreadOwner::Reaping;
  change();
  return true;
}

bool ThreadRegistryLock::leases_drained(const ThreadControl &c) const {
  return belongs(c) && !c.leases_open && !c.leases;
}

bool ThreadRegistryLock::abort_ready(ThreadControl &c) {
  if (!belongs(c) || c.owner != ThreadOwner::AbortOwner ||
      c.execution != ThreadExecution::Starting || !leases_drained(c))
    return false;
  c.execution = ThreadExecution::ExitReady;
  change();
  return true;
}

bool ThreadRegistryLock::finish_reaping(ThreadControl &c) {
  if (!belongs(c) || c.queued || c.owner != ThreadOwner::Reaping || c.creator_pin ||
      c.api_pins || c.leases)
    return false;
  c.owner = ThreadOwner::Reaped;
  if (c.internal_helper)
    --registry.unreaped_helpers;
  if (c.callback_phase == CallbackPhase::Discarded ||
      c.callback_phase == CallbackPhase::Active)
    --registry.unreaped_callbacks;
  c.lifecycle_pin = false;
  change();
  // The exclusive reclaimer copies descriptors before this transfer. It may
  // unmap only after unlocking, and must retain failed-unmap ownership itself.
  return true;
}

void ThreadRegistryLock::enqueue_completed(ThreadControl &c) {
  if (c.owner != ThreadOwner::Detached ||
      c.execution != ThreadExecution::ExitReady || c.queued ||
      c.internal_helper)
    return;
  if (!requeue_candidate(c))
    __builtin_trap();
  change();
}

bool ThreadRegistryLock::requeue_candidate(ThreadControl &c) {
  if (!belongs(c) || c.queued || c.internal_helper ||
      c.execution != ThreadExecution::ExitReady ||
      (c.owner != ThreadOwner::Detached && c.owner != ThreadOwner::Reaping))
    return false;
  c.queued = true;
  c.reap_next = nullptr;
  if (registry.reap_tail)
    registry.reap_tail->reap_next = &c;
  else
    registry.reap_head = &c;
  registry.reap_tail = &c;
  ++registry.reap_count;
  return true;
}

ThreadControl *ThreadRegistryLock::take_reap_candidate() {
  ThreadControl *c = registry.reap_head;
  if (!c)
    return nullptr;
  registry.reap_head = c->reap_next;
  if (!registry.reap_head)
    registry.reap_tail = nullptr;
  c->reap_next = nullptr;
  c->queued = false;
  --registry.reap_count;
  return c;
}

void ThreadRegistryLock::remove_queued(ThreadControl &c) {
  if (!c.queued)
    return;
  ThreadControl **link = &registry.reap_head, *previous = nullptr;
  while (*link != &c) {
    previous = *link;
    link = &(*link)->reap_next;
  }
  *link = c.reap_next;
  if (registry.reap_tail == &c)
    registry.reap_tail = previous;
  c.reap_next = nullptr;
  c.queued = false;
  --registry.reap_count;
}

bool ThreadRegistryLock::claim_process_exit() {
  if (registry.live || registry.reservations)
    return false;
  uint64_t expected = ThreadRegistry::RUNNING;
  if (!registry.process_exit_state.compare_exchange_strong(
          expected, ThreadRegistry::EXITING, cpp::MemoryOrder::ACQ_REL))
    return false;
  change();
  return true;
}

bool ThreadRegistry::claim_process_cleanup(bool sole_fork_child) {
  // Closing creation and electing cleanup are one atomic transition. An
  // admission already past its acquire check may finish, but new checks fail.
  auto previous =
      process_exit_state.exchange(CLEANUP_CLAIMED, cpp::MemoryOrder::ACQ_REL);
  return sole_fork_child || previous != CLEANUP_CLAIMED;
}

bool ThreadRegistryLock::claim_process_cleanup() {
  if (!registry.claim_process_cleanup())
    return false;
  change();
  return true;
}

ThreadTermination
ThreadRegistryLock::termination_action(ThreadControl &c) {
  if (!belongs(c) || c.counted ||
      c.execution != ThreadExecution::ExitReady)
    __builtin_trap();
  if (c.internal_helper || c.callback_phase == CallbackPhase::Discarded)
    return ThreadTermination::ThreadOnly;
  if (registry.process_exiting() || registry.live)
    return ThreadTermination::ThreadOnly;
  // Keep this application context alive until a pending creation either
  // contributes a live peer or rolls back its reservation.
  if (registry.reservations)
    return ThreadTermination::WaitForCreation;
  // Explicit exit can close creation without this lock between the check and
  // the last-thread CAS. Its owner will perform process cleanup in that case.
  if (!claim_process_exit())
    return ThreadTermination::ThreadOnly;
  c.last_worker_exit_elected = !c.initial_thread && !c.process_lifetime &&
                              (c.owner == ThreadOwner::Joinable ||
                               c.owner == ThreadOwner::Detached);
  return ThreadTermination::Process;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
