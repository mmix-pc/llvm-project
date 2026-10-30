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

bool ThreadRegistry::pristine_fork_caller(ThreadControl &caller) {
  if (!mutex.value.try_lock())
    return false;
  bool pristine = main_registered && !creation_started && !process_operation &&
                  !process_exiting() && !fork_recovery.is_child() &&
                  !caller.fork_recovery.is_child() && !reservations &&
                  !helper_reservations && !unreaped_helpers &&
                  !unreaped_callbacks &&
                  live == 1 && head == &caller && !caller.next &&
                  !reap_head && !reap_tail && !reap_count &&
                  caller.registry == this && caller.listed &&
                  caller.execution == ThreadExecution::Running &&
                  !caller.callbacks_started;
  mutex.value.unlock();
  return pristine;
}

bool ThreadRegistry::freeze_for_fork(ThreadControl &caller, uint64_t suppression,
                                    bool *single_thread, bool helper_drained) {
  if (single_thread)
    *single_thread = false;
  mutex.value.lock();
  if (process_exiting() || reservations || helper_reservations ||
      process_operation ||
      caller.registry != this || !caller.listed ||
      caller.execution != ThreadExecution::Running || caller.callbacks_started ||
      !caller.fork_recovery.allows_fork() || caller.cancellation_transfer.load() ||
      caller.cancellation_suppression.load() != suppression ||
      caller.cancellation_point.load()) {
    mutex.value.unlock();
    return false;
  }
  // The census is necessary, but resource release after final reap must still
  // be witnessed independently by the caller through helper_drained.
  if (single_thread)
    *single_thread = (!creation_started || helper_drained) &&
                     !unreaped_helpers && !unreaped_callbacks &&
                     live == 1 && head == &caller &&
                     !caller.next && !reap_head && !reap_count;
  return true;
}

void ThreadRegistry::release_after_fork() { mutex.value.unlock(); }

void ThreadRegistry::restore_fork_child(ThreadControl &survivor,
                                      internal::ForkRecovery recovery) {
  if (!recovery.is_child())
    __builtin_trap();
  // Only the survivor exists in this copied address space. Do not traverse or
  // reclaim vanished stacks, copied pins, leases or reaper queue entries.
  head = &survivor;
  reap_head = reap_tail = nullptr;
  reap_count = reservations = unreaped_callbacks = 0;
  helper_reservations = unreaped_helpers = 0;
  live = 1;
  process_exit_state.store(RUNNING, cpp::MemoryOrder::RELAXED);
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
  survivor.internal_helper = survivor.callbacks_started = false;
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
  return registry.main_registered && !registry.process_exiting() &&
         !registry.process_operation && !registry.reservations &&
         !registry.helper_reservations &&
         !registry.unreaped_callbacks &&
         registry.live == 1 && registry.head == &caller && !caller.next &&
         !registry.reap_head && !registry.reap_count &&
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
  if (c.attributes.style == ThreadStyle::POSIX)
    result = ThreadReturnValue(c.attributes.retval.posix_retval);
  else if (c.attributes.style == ThreadStyle::STDC)
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

bool ThreadRegistryLock::begin_cleanup(ThreadControl &c, bool cancellation) {
  if (!belongs(c) || c.execution != ThreadExecution::Running ||
      c.callback_phase == CallbackPhase::Waiting ||
      !c.cancellation.claim(cancellation))
    return false;
  c.execution = ThreadExecution::Cleaning;
  return true;
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
ThreadRegistryLock::termination_action(const ThreadControl &c) {
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
  return claim_process_exit() ? ThreadTermination::Process
                             : ThreadTermination::ThreadOnly;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
