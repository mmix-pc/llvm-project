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

#include "src/__support/threads/linux/mmix/tls.h"
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
enum class ChildStatus : uint32_t { Starting, Ready, Failed };
enum class CreatorDecision : uint32_t { Hold, Go, Abort };

struct ThreadMapping {
  uintptr_t base = 0;
  size_t size = 0;
};

class ThreadRegistry;
class ThreadRegistryLock;

struct ThreadControl {
  // Thread::attrib identifies this embedded object. Lookup compares addresses;
  // it must never use container_of on an unpinned caller-supplied handle.
  ThreadAttributes attributes;
  ThreadRunner runner{};
  void *argument = nullptr;
  uint64_t saved_signal_mask = 0;
  ThreadMapping control_mapping, stack_mapping;
  TLSDescriptor tls{};
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

private:
  friend class ThreadRegistryLock;
  ThreadRegistry *registry = nullptr;
  ThreadControl *next = nullptr;
  ThreadOwner owner = ThreadOwner::Unpublished;
  ThreadExecution execution = ThreadExecution::Starting;
  bool creator_pin = true, lifecycle_pin = true;
  bool listed = false, leases_open = false, counted = false;
  bool process_lifetime = false;
  bool internal_helper = false;
  size_t api_pins = 0, leases = 0;
};

// These objects outlive every control mapping, including the reaper's.
class ThreadRegistry {
  friend class ThreadRegistryLock;
  // RawMutex contains a 32-bit futex; reserve its complete containing octa.
  struct alignas(8) RegistryMutex {
    RawMutex value;
  } mutex;
  LifecycleWord event;
  ThreadControl *head = nullptr;
  size_t live = 0, reservations = 0;
  bool exiting = false;
  bool main_registered = false;

public:
  constexpr ThreadRegistry() = default;
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
  void change() { changed = true; }

public:
  explicit ThreadRegistryLock(ThreadRegistry &registry);
  ~ThreadRegistryLock();
  ThreadRegistryLock(const ThreadRegistryLock &) = delete;
  ThreadRegistryLock &operator=(const ThreadRegistryLock &) = delete;

  uint32_t sequence();
  bool reserve();
  bool cancel_reservation();
  // Bootstrap only, before user callbacks or worker publication. Main uses
  // static control storage and the kernel's non-owned initial stack.
  bool register_main(ThreadControl &control);
  // Register a successful creation, consuming one reservation. The helper is
  // explicitly excluded from application accounting and needs no reservation.
  bool insert(ThreadControl &control, bool detached, bool helper = false);
  bool adopt_abort(ThreadControl &control);
  ThreadControl *pin(ThreadAttributes *handle);
  bool unpin(ThreadControl &control);
  bool drop_creator(ThreadControl &control);
  bool claim_join(ThreadControl &control);
  bool release_join(ThreadControl &control);
  bool join_result(ThreadControl &control, ThreadReturnValue &result) const;
  bool detach(ThreadControl &control);
  bool start(ThreadControl &control);
  bool begin_cleanup(ThreadControl &control);
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
  ThreadOwner owner(const ThreadControl &control) const {
    return control.owner;
  }
  size_t live_threads() const { return registry.live; }
  size_t pending_creations() const { return registry.reservations; }
  bool process_exiting() const { return registry.exiting; }
  bool claim_process_exit();
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
