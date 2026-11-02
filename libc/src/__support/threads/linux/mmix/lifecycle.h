//===-- MMIX Linux thread lifetime primitives --------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_LIFECYCLE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_LIFECYCLE_H

#include "priority_protection_state.h"
#include "src/__support/OSUtil/linux/fork_recovery.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/__support/OSUtil/linux/loader_services.h"
#endif
#include "src/__support/threads/linux/mmix/cancellation_state.h"
#include "src/__support/threads/linux/mmix/tls.h"
#include "src/__support/threads/linux/mmix/unwind_root.h"
#include "src/__support/threads/raw_mutex.h"
#include "src/__support/threads/thread_attributes.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// A kernel int occupies the first four bytes, not the low half of an octa.
// Isolate every word from adjacent subword atomic read-modify-write operations.
struct alignas(8) LifecycleWord {
  Futex value;
  uint32_t reserved = 0;
  constexpr explicit LifecycleWord(uint32_t initial = 0) : value(initial) {}
};
static_assert(sizeof(LifecycleWord) == 8 && alignof(LifecycleWord) == 8);
static_assert(__builtin_offsetof(LifecycleWord, value) == 0);
static_assert(sizeof(Futex) == 4);

enum class ThreadOwner {
  Unpublished,
  Joinable,
  JoinOwner,
  Detached,
  AbortOwner,
  Reaping,
  Reaped
};
enum class ThreadExecution { Starting, Running, Cleaning, ExitReady };
enum class ThreadTermination { ThreadOnly, WaitForCreation, Process };
enum class ChildStatus : uint32_t { Starting, Ready, Failed };
enum class CreatorDecision : uint32_t { Hold, Go, Abort };
enum class CallbackPhase { None, Waiting, Active, Discarded };
enum class ForkContinuation {
  Ordinary,
  NormalDuring,
  NormalAfter,
  InitialThreadDuring,
  LastThreadAfter,
  LastWorkerAfter,
  WorkerThreadDuring
};
enum class ForkSnapshot { Recoverable, TerminalExecOnly };

struct ThreadMapping {
  uintptr_t base = 0;
  size_t size = 0;
};

// Internal resource ownership, never an application callback. Preparation
// acquires a separate reference; reclamation releases it after all mappings.
// Both hooks return positive errno on failure without consuming ownership.
// Context storage must outlive the thread's control, stack and TLS; hooks must
// be resident runtime code, synchronize shared ownership, and never unwind,
// cancel or invoke user code. A copied resource descriptor is not a new owner.
struct ThreadResourceOwner {
  void *context = nullptr;
  long (*retain)(void *) = nullptr;
  long (*release)(void *) = nullptr;

  // Opt-in for a process-private invocation retained through post-stack
  // TLS/TSS drain and process finalization. Copying terminal continuations
  // preserves the thread reference and image pin without ownership hooks.
  // Registry census, drain/root, leases and reclamation guards still apply.
  ThreadRunnerPosix *terminal_runner = nullptr;

  bool valid() const {
    return context ? retain && release : !retain && !release && !terminal_runner;
  }
};

class ThreadRegistry;
class ThreadRegistryLock;
struct CancellationPoint;

struct ThreadControl {
  // Separate from the public handle registry: internal helpers participate too.
  ThreadControl *credential_next = nullptr;
  bool credential_registered = false;
  alignas(8) Futex credential_pending{0};
  long credential_result = 0;
  // Thread::attrib identifies this embedded object. Lookup compares addresses;
  // it must never use container_of on an unpinned caller-supplied handle.
  ThreadAttributes attributes;
  ThreadRunner runner{};
  void *argument = nullptr;
  uint64_t saved_signal_mask = 0;
  ThreadMapping control_mapping, stack_mapping;
  ThreadResourceOwner resource_owner;
  TLSDescriptor tls{};
#ifdef LIBC_MMIX_SHARED_STARTUP
  loader::ThreadTLSAllocation shared_tls{};
  cpp::Atomic<uint64_t> loader_scopes{0}, loader_callbacks{0};
  // Established before user callbacks, independently of unwind-root capture.
  uint64_t loader_chain = 0;
#endif
  UnwindRoot unwind_root{};
  PriorityProtectionState priority_protection;
  bool apply_scheduling = false;
  internal::ForkRecovery fork_recovery;
  int scheduling_policy = 0, scheduling_priority = 0;
  CancellationState cancellation;
  cpp::Atomic<CancellationPoint *> cancellation_point{nullptr};
  cpp::Atomic<uint64_t> cancellation_suppression{0}, cancellation_transfer{0};
  uint64_t cancellation_identity = 0, cancellation_generation = 0;
  uintptr_t stack_bottom = 0, stack_top = 0;
  size_t guard_size = 0;
  bool owns_stack = false;
  LifecycleWord parent_tid;
  LifecycleWord clear_tid{UINT32_MAX};
  LifecycleWord prepared;
  LifecycleWord child_status{uint32_t(ChildStatus::Starting)};
  LifecycleWord creator_decision{uint32_t(CreatorDecision::Hold)};
  int child_error = 0;

  void publish_prepared() {
    prepared.value.store(1, cpp::MemoryOrder::RELEASE);
  }
  bool is_prepared() { return prepared.value.load(cpp::MemoryOrder::ACQUIRE); }
  void publish_child(ChildStatus status, int error = 0) {
    child_error = error;
    child_status.value.store(uint32_t(status), cpp::MemoryOrder::RELEASE);
  }
  ChildStatus acquire_child() {
    return ChildStatus(child_status.value.load(cpp::MemoryOrder::ACQUIRE));
  }
  void publish_decision(CreatorDecision decision) {
    creator_decision.value.store(uint32_t(decision), cpp::MemoryOrder::RELEASE);
  }
  CreatorDecision acquire_decision() {
    return CreatorDecision(
        creator_decision.value.load(cpp::MemoryOrder::ACQUIRE));
  }

  bool retains_resources_until_process_exit() const { return process_lifetime; }
  // Owner-only check after the registry isolated a terminal copied child.
  bool terminal_exec_only_survivor() const {
    return fork_recovery.is_child() && !fork_recovery.allows_fork() &&
           process_lifetime && callbacks_started &&
           callback_phase == CallbackPhase::None &&
           execution == ThreadExecution::Running && !unwind_root.active;
  }
  bool terminal_cleanup_pending() {
    return initial_thread && process_lifetime &&
           execution == ThreadExecution::Cleaning && cancellation.terminal();
  }
  bool terminal_cleanup_in_progress() {
    return terminal_cleanup_pending() && callbacks_started;
  }
  bool terminal_resources_retained() const {
    return resource_owner.valid() &&
           (!resource_owner.context ||
            (resource_owner.terminal_runner &&
             attributes.style == ThreadStyle::POSIX &&
             runner.posix_runner == resource_owner.terminal_runner &&
             argument == resource_owner.context));
  }
  bool retained_active_callback() const {
    return callback_phase == CallbackPhase::Active &&
           owner == ThreadOwner::Detached && !initial_thread &&
           !process_lifetime && !internal_helper && resource_owner.context &&
           terminal_resources_retained();
  }
  // Original worker only. Snapshot ownership/census checks remain under the
  // registry lock; this predicate does not elect a last thread or close leases.
  bool worker_cleanup_pending() {
    return !initial_thread && !process_lifetime && !internal_helper &&
           (callback_phase == CallbackPhase::None || retained_active_callback()) &&
           execution == ThreadExecution::Cleaning && cancellation.terminal() &&
           !completed_worker_callback_mgr && !last_worker_exit_elected &&
           terminal_resources_retained();
  }
  bool worker_cleanup_in_progress() {
    return worker_cleanup_pending() && callbacks_started;
  }
  // Owner-only local half of the terminal certificate. Registry-owned pins,
  // queues, leases and ownership are inspected only under the snapshot lock.
  bool terminal_cleanup_finished() {
    return initial_thread && process_lifetime && callbacks_started &&
           execution == ThreadExecution::ExitReady &&
           cancellation.terminal();
  }

  // Owner-only, called only after complete_thread_cleanup's TLS/TSS drain
  // returns. Starting callbacks is not a completed-drain certificate.
  bool record_worker_cleanup_finished() {
    if (initial_thread || process_lifetime || internal_helper ||
        (callback_phase != CallbackPhase::None && !retained_active_callback()) ||
        execution != ThreadExecution::Cleaning || !callbacks_started ||
        !cancellation.terminal() || !attributes.atexit_callback_mgr ||
        completed_worker_callback_mgr)
      return false;
    completed_worker_callback_mgr = attributes.atexit_callback_mgr;
    return true;
  }

  // Independent of main's phase and initial identity. Ownership, pins and the
  // sole-task census still require the registry snapshot lock.
  bool last_worker_cleanup_finished() {
    return !initial_thread && !process_lifetime && !internal_helper &&
           (callback_phase == CallbackPhase::None || retained_active_callback()) &&
           callbacks_started &&
           completed_worker_callback_mgr &&
           completed_worker_callback_mgr == attributes.atexit_callback_mgr &&
           last_worker_exit_elected &&
           execution == ThreadExecution::ExitReady && cancellation.terminal() &&
           terminal_resources_retained();
  }

  // Only the owning thread may access this guard, including recursive exit.
  bool begin_callback_cleanup() {
    if (callbacks_started)
      return false;
    callbacks_started = true;
    return true;
  }

private:
  friend class ThreadRegistry;
  friend class ThreadRegistryLock;
  ThreadRegistry *registry = nullptr;
  ThreadControl *next = nullptr;
  ThreadControl *reap_next = nullptr;
  bool queued = false;
  ThreadOwner owner = ThreadOwner::Unpublished;
  ThreadExecution execution = ThreadExecution::Starting;
  bool creator_pin = true, lifecycle_pin = true;
  bool listed = false, leases_open = false, counted = false;
  bool process_lifetime = false;
  bool initial_thread = false;
  bool internal_helper = false;
  CallbackPhase callback_phase = CallbackPhase::None;
  bool callbacks_started = false;
  ThreadAtExitCallbackMgr *completed_worker_callback_mgr = nullptr;
  bool last_worker_exit_elected = false;
  size_t api_pins = 0, leases = 0;
};

// Set only after the current task has published its native thread state.
extern LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control;

// These objects outlive every control mapping, including the reaper's.
class ThreadRegistry {
  friend class ThreadRegistryLock;
  // RawMutex contains a 32-bit futex; reserve its complete containing octa.
  struct alignas(8) RegistryMutex {
    RawMutex value;
  } mutex;
  LifecycleWord event;
  ThreadControl *head = nullptr;
  ThreadControl *reap_head = nullptr, *reap_tail = nullptr;
  size_t reap_count = 0;
  size_t live = 0, reservations = 0;
  // Helpers never contribute to application lifetime. Keep their creation and
  // unreaped ownership visible to fork admission independently of that census.
  size_t helper_reservations = 0, unreaped_helpers = 0;
  // Precreated callbacks remain counted through activation until final reap.
  // Zero alone does not prove quiescence: reaper/owner release must drain too.
  size_t unreaped_callbacks = 0;
  static constexpr uint64_t RUNNING = 0, EXITING = 1, CLEANUP_CLAIMED = 2;
  alignas(8) cpp::Atomic<uint64_t> process_exit_state{RUNNING};
  bool process_exiting() {
    return process_exit_state.load(cpp::MemoryOrder::ACQUIRE) != RUNNING;
  }
  bool main_registered = false;
  bool creation_started = false;
  bool process_operation = false;
  internal::ForkRecovery fork_recovery;
  // A retained active invocation owns exactly one unreaped callback slot.
  // Other dormant/discarded callbacks may be absent from the application list.
  bool terminal_callback_census(const ThreadControl &caller) const {
    return (caller.callback_phase == CallbackPhase::None &&
            !unreaped_callbacks) ||
           (caller.retained_active_callback() && unreaped_callbacks == 1);
  }
  bool terminal_fork_caller(ThreadControl &caller, ForkContinuation kind);
  bool terminal_during_fork_caller(ThreadControl &caller, ForkContinuation kind,
                                 ForkSnapshot snapshot);

public:
  constexpr ThreadRegistry() = default;
  // Lock-free explicit-exit election, including from an interrupted registry
  // owner. A restricted fork child has no peers and may replace a copied claim;
  // its caller must reject recursion before requesting that override.
  bool claim_process_cleanup(bool sole_fork_child = false);
  // Lock-free notification admission, linearized against exit election. An
  // attempt admitted before election may finish; later attempts (including
  // retries) must not deliver new notifications. This is not a lifetime pin.
  bool admit_signal_notification() { return !process_exiting(); }
  // Nonblocking precheck; the caller has blocked signals and proved that it
  // is on its ordinary chain before inspecting the registry.
  bool quiescent_fork_caller(ThreadControl &caller);
  // Dedicated clone snapshot scope; never use the ordinary API lock across it.
  // Non-ordinary kinds require the matching private exit-owner/root
  // certificate. Both terminal DURING kinds retain Cleaning/live one/RUNNING;
  // LastThreadAfter requires the completed initial-thread drain and live zero.
  // LastWorkerAfter instead requires an elected, completed ordinary worker;
  // neither terminal AFTER kind promotes identity or transfers backing.
  // All terminal certificates use nonblocking registry acquisition.
  // TerminalExecOnly admits only certified DURING owners, with suppression one.
  // Helpers need not be drained, but single_thread remains false. The caller
  // must independently freeze/quarantine service state and validate its root.
  // Its registry acquisition is nonblocking, including for admission prechecks.
  bool freeze_for_fork(
      ThreadControl &caller, uint64_t suppression = 0,
      bool *single_thread = nullptr, bool helper_drained = false,
      ForkContinuation continuation = ForkContinuation::Ordinary,
      ForkSnapshot snapshot = ForkSnapshot::Recoverable);
  void release_after_fork();
  // TerminalExecOnly requires ExecOnly recovery and preserves the active fork
  // guard's suppression while invalidating copied terminal execution state.
  void restore_fork_child(
      ThreadControl &survivor,
      internal::ForkRecovery recovery = internal::ForkRecovery::child(),
      ForkContinuation continuation = ForkContinuation::Ordinary,
      ForkSnapshot snapshot = ForkSnapshot::Recoverable);
  // Snapshot under ThreadRegistryLock; wait only after releasing it. Callers
  // supply a bounded deadline and recheck predicates even after timeout/wrap.
  ErrorOr<int> wait(uint32_t sequence, Futex::Timeout deadline) {
    return event.value.wait(sequence, deadline);
  }
};

extern ThreadRegistry thread_registry;

// Callers block asynchronous signals and cancellation before locking. No
// callbacks, target operations, waits or unmapping may occur in this scope.
// Control arguments require a creator, lifecycle or acquired API pin.
class ThreadRegistryLock {
  ThreadRegistry &registry;
  bool changed = false;
  bool belongs(const ThreadControl &control) const;
  void enqueue_completed(ThreadControl &control);
  void remove_queued(ThreadControl &control);
  void change() { changed = true; }

public:
  explicit ThreadRegistryLock(ThreadRegistry &registry);
  ~ThreadRegistryLock();
  ThreadRegistryLock(const ThreadRegistryLock &) = delete;
  ThreadRegistryLock &operator=(const ThreadRegistryLock &) = delete;

  uint32_t sequence();
  bool reserve();
  bool begin_helper_creation();
  bool sole_fork_caller(const ThreadControl &caller) const;
  bool begin_process_operation(const ThreadControl &caller);
  bool begin_vfork(const ThreadControl &caller);
  bool end_process_operation();
  bool cancel_reservation();
  bool cancel_helper_reservation();
  // Bootstrap only, before user callbacks or worker publication. Main uses
  // static control storage and the kernel's non-owned initial stack.
  bool register_main(ThreadControl &control);
  // Register a successful creation, consuming its application/helper
  // reservation. Helpers are excluded from application accounting.
  bool insert(ThreadControl &control, bool detached, bool helper = false);
  // A reserved managed thread waits without a public handle or application
  // lifetime contribution. Its creator pin protects registration until a
  // one-shot activation/discard decision has been published and acknowledged.
  bool insert_callback_waiter(ThreadControl &control);
  bool activate_callback(ThreadControl &control);
  bool discard_callback(ThreadControl &control);
  CallbackPhase callback_phase(const ThreadControl &control) const {
    return control.callback_phase;
  }
  bool adopt_abort(ThreadControl &control);
  ThreadControl *pin(ThreadAttributes *handle);
  bool unpin(ThreadControl &control);
  bool drop_creator(ThreadControl &control);
  bool claim_join(ThreadControl &control);
  bool release_join(ThreadControl &control);
  bool join_result(ThreadControl &control, ThreadReturnValue &result) const;
  bool detach(ThreadControl &control);
  bool start(ThreadControl &control);
  bool begin_cleanup(ThreadControl &control, bool cancellation = false);
  // Peers own a pin and lease; owner=true requires the current live activation.
  int request_cancellation(ThreadControl &control, bool owner = false);
  bool exit_ready(ThreadControl &control, ThreadReturnValue result);
  bool acquire_lease(ThreadControl &control);
  bool release_lease(ThreadControl &control);
  bool close_leases(ThreadControl &control);
  bool leases_drained(const ThreadControl &control) const;
  bool abort_ready(ThreadControl &control);
  // The exclusive owner calls this only after the terminal observation.
  // Normal completion additionally requires cleanup/result publication.
  bool begin_reaping(ThreadControl &control);
  bool finish_reaping(ThreadControl &control);
  // The sole reaper borrows lifecycle ownership when taking a candidate.
  // Rotation alone does not notify the event and cannot create a busy loop.
  size_t queued_reclaims() const { return registry.reap_count; }
  ThreadControl *take_reap_candidate();
  bool requeue_candidate(ThreadControl &control);
  ThreadOwner owner(const ThreadControl &control) const {
    return control.owner;
  }
  size_t live_threads() const { return registry.live; }
  size_t pending_creations() const { return registry.reservations; }
  size_t pending_helper_creations() const {
    return registry.helper_reservations;
  }
  bool process_exiting() const { return registry.process_exiting(); }
  bool claim_process_exit();
  // Explicit exit may win while peers remain. Last-thread election only closes
  // creation; both paths must claim this ownership before running callbacks.
  bool claim_process_cleanup();
  ThreadTermination termination_action(ThreadControl &control);
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
