//===-- MMIX private signal boundary ownership -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_SIGNAL_BOUNDARY_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_SIGNAL_BOUNDARY_H

#include "src/__support/macros/config.h"
#include <libunwind.h>
#include <stddef.h>
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl;
struct SignalBoundary;
struct SignalBoundaryDisposal;
struct SignalParentHandoff;

// Private controlled-wrapper protocol, not public signal/cancellation
// admission. Preconditions for EVERY operation:
// * One stable owner per thread lifetime, never copied/reset to reuse generations.
//   All participating signals are blocked and no other thread accesses the
//   state. Keep that mask through detach, kernel jump and parent consumption.
//   No helper changes a mask, pending cancellation, cleanup head or ordinary
//   unwind root.
// * Thread/chain observations and the wrapper extent come from trusted capture.
//   The kernel establishes the current child's DIRECT suspended parent. Capture
//   the parent context before application code can modify public ucontext
//   bytes. This component does NOT prove public ucontext authenticity or kernel
//   ancestry.
// * Linked records are initialized, live, caller-owned wrapper storage. The
//   owner and handoff storage survive child activation/stack retirement,
//   including alternate-stack retirement. Numeric disjointness alone cannot
//   prove this.
// * Disposal certificates attest actual completed cleanup and removal of ALL
//   child cursor/lease/query/observer links. No callback is performed here.
//   A boolean supplied inside an arbitrary unwind stop is NOT such evidence:
//   escape/pin is not cursor finish. The consumer must establish actual managed
//   cursor completion and query restoration before issuing this certificate.
//   A static probe without _LIBUNWIND_MMIX_STARTUP_IMAGES has no such leased
//   sidecars; that configuration-specific fact is not shared-runtime disposal.
//   After prepare succeeds, do not reacquire child cursor/observer ownership;
//   proceed to detach and the kernel jump with the certificate still valid.
// These are serialized metadata operations, not an async-safe metadata
// unwinder, a kernel transfer implementation, or a terminal cancellation claim.
// Rejection returns false without mutation. After successful preparation there
// is no normal-return rollback; transfer failure must fail closed.

struct SignalBoundaryFrame {
  uintptr_t ro = 0, procedure = 0, sp = 0, limit = 0;

  bool contains(const void *storage, size_t size) const {
    uintptr_t address = reinterpret_cast<uintptr_t>(storage);
    return ro && !(ro & 7) && procedure && !(procedure & 3) && sp &&
           !(sp & 7) && !(limit & 7) && sp < limit && address >= sp &&
           address < limit && size <= limit - address;
  }

  bool disjoint(const void *storage, size_t size) const {
    uintptr_t address = reinterpret_cast<uintptr_t>(storage);
    return address >= limit || (address <= sp && size <= sp - address);
  }

  bool matches(const SignalBoundaryFrame &other) const {
    return ro == other.ro && procedure == other.procedure && sp == other.sp &&
           limit == other.limit;
  }
};

struct SignalBoundaryPoint {
  const ThreadControl *thread = nullptr;
  uint64_t child_chain = 0, parent_chain = 0, generation = 0;
  SignalBoundaryFrame wrapper;

  bool matches(const SignalBoundaryPoint &other) const {
    // Establish same-chain ownership BEFORE comparing register-stack positions.
    return thread == other.thread && child_chain == other.child_chain &&
           parent_chain == other.parent_chain &&
           generation == other.generation && wrapper.matches(other.wrapper);
  }
};

struct SignalBoundaryOwner {
  const ThreadControl *thread;
  uint64_t base_chain;
  uint64_t generation = 0; // High-water mark, never decremented or wrapped.
  SignalBoundary *live = nullptr;
  uint64_t live_generation = 0;
  uint64_t handoff_generation = 0;

  constexpr SignalBoundaryOwner(const ThreadControl *thread, uint64_t base_chain)
      : thread(thread), base_chain(base_chain) {}
  SignalBoundaryOwner(const SignalBoundaryOwner &) = delete;
  SignalBoundaryOwner &operator=(const SignalBoundaryOwner &) = delete;

  bool enter(SignalBoundary &, const SignalBoundaryPoint &,
             uintptr_t interrupted_context, const void *cleanup_boundary,
             const unw_context_t &saved_parent);
  bool normal_return(SignalBoundary &, const SignalBoundaryPoint &);
  bool prepare_handoff(SignalBoundary &, const SignalBoundaryPoint &,
                       const SignalBoundaryDisposal &, SignalParentHandoff &);
  bool detach(SignalBoundary &, const SignalBoundaryPoint &,
              SignalParentHandoff &);
  bool consume(SignalParentHandoff &, const ThreadControl *current_thread,
               uint64_t current_chain, uint64_t expected_generation);

private:
  bool valid() const;
  bool matches(const SignalBoundary &, const SignalBoundaryPoint &) const;
  void pop(SignalBoundary &);
};

struct SignalBoundary {
  SignalBoundaryOwner *owner = nullptr;
  SignalBoundary *previous = nullptr;
  uint64_t previous_generation = 0;
  SignalBoundaryPoint point;
  uintptr_t interrupted_context = 0; // Locator only; never dereferenced here.
  const void *cleanup_boundary = nullptr; // Null is a valid empty checkpoint.
  unw_context_t saved_parent{};

  SignalBoundary() = default;
  SignalBoundary(const SignalBoundary &) = delete;
  SignalBoundary &operator=(const SignalBoundary &) = delete;
};

struct SignalBoundaryDisposal {
  const SignalBoundaryOwner *owner = nullptr;
  uint64_t generation = 0, child_chain = 0;
  const void *cleanup_boundary = nullptr;
  bool completed_segment = false;
  bool cursors_disposed = false;
  bool observers_disposed = false;
};

struct SignalParentHandoff {
  enum class Phase { Empty, Prepared, Detached, Consumed };
  Phase phase = Phase::Empty;
  // Only the independently live owner, never a child record or a self-pointer.
  const SignalBoundaryOwner *owner = nullptr;
  uint64_t generation = 0, child_chain = 0, parent_chain = 0;
  unw_context_t saved_parent{};

  SignalParentHandoff() = default;
  SignalParentHandoff(const SignalParentHandoff &) = delete;
  SignalParentHandoff &operator=(const SignalParentHandoff &) = delete;
};

inline bool SignalBoundaryOwner::valid() const {
  if (!thread || !base_chain || bool(live) != bool(live_generation) ||
      handoff_generation > generation)
    return false;
  uint64_t expected = live_generation, newer = 0;
  for (auto *record = live; record; record = record->previous) {
    const auto &point = record->point;
    if (record->owner != this || point.thread != thread || !point.generation ||
        point.generation != expected || point.generation > generation ||
        (newer && point.generation >= newer) || !point.child_chain ||
        !point.parent_chain || point.child_chain == base_chain ||
        point.child_chain == point.parent_chain ||
        !point.wrapper.contains(record, sizeof(*record)) ||
        !point.wrapper.disjoint(this, sizeof(*this)) ||
        !record->interrupted_context || (record->interrupted_context & 7) ||
        bool(record->previous) != bool(record->previous_generation))
      return false;
    if (record->previous) {
      if (point.parent_chain != record->previous->point.child_chain)
        return false;
    } else if (point.parent_chain != base_chain) {
      return false;
    }
    newer = point.generation;
    expected = record->previous_generation;
  }
  return true;
}

inline bool
SignalBoundaryOwner::matches(const SignalBoundary &record,
                             const SignalBoundaryPoint &point) const {
  return valid() && live == &record && record.owner == this &&
         live_generation == point.generation && point.thread == thread &&
         record.point.matches(point);
}

inline bool SignalBoundaryOwner::enter(SignalBoundary &record,
                                       const SignalBoundaryPoint &point,
                                       uintptr_t interrupted_context,
                                       const void *cleanup_boundary,
                                       const unw_context_t &saved_parent) {
  if (!valid() || handoff_generation || record.owner ||
      point.thread != thread || generation == UINT64_MAX ||
      point.generation != generation + 1 || !point.child_chain ||
      point.child_chain == base_chain ||
      point.parent_chain != (live ? live->point.child_chain : base_chain) ||
      point.child_chain == point.parent_chain ||
      !point.wrapper.contains(&record, sizeof(record)) ||
      !point.wrapper.disjoint(this, sizeof(*this)) || !interrupted_context ||
      (interrupted_context & 7))
    return false;
  for (auto *ancestor = live; ancestor; ancestor = ancestor->previous)
    if (ancestor == &record || ancestor->point.child_chain == point.child_chain)
      return false;

  record.previous = live;
  record.previous_generation = live_generation;
  record.point = point;
  record.interrupted_context = interrupted_context;
  record.cleanup_boundary = cleanup_boundary;
  record.saved_parent = saved_parent;
  record.owner = this;
  generation = point.generation;
  live_generation = point.generation;
  live = &record;
  return true;
}

inline void SignalBoundaryOwner::pop(SignalBoundary &record) {
  live = record.previous;
  live_generation = record.previous_generation;
  record.owner = nullptr;
  record.previous = nullptr;
  record.previous_generation = 0;
}

inline bool
SignalBoundaryOwner::normal_return(SignalBoundary &record,
                                   const SignalBoundaryPoint &point) {
  if (handoff_generation || !matches(record, point))
    return false;
  pop(record);
  return true;
}

inline bool SignalBoundaryOwner::prepare_handoff(
    SignalBoundary &record, const SignalBoundaryPoint &point,
    const SignalBoundaryDisposal &disposal, SignalParentHandoff &handoff) {
  if (handoff_generation || !matches(record, point) ||
      handoff.phase != SignalParentHandoff::Phase::Empty ||
      !point.wrapper.disjoint(&handoff, sizeof(handoff)) ||
      disposal.owner != this || disposal.generation != live_generation ||
      disposal.child_chain != point.child_chain ||
      disposal.cleanup_boundary != record.cleanup_boundary ||
      !disposal.completed_segment || !disposal.cursors_disposed ||
      !disposal.observers_disposed)
    return false;
  handoff.owner = this;
  handoff.generation = live_generation;
  handoff.child_chain = point.child_chain;
  handoff.parent_chain = point.parent_chain;
  handoff.saved_parent = record.saved_parent;
  handoff.phase = SignalParentHandoff::Phase::Prepared;
  handoff_generation = live_generation;
  return true;
}

// Call immediately before the nonreturning kernel jump. After success, no path
// may resume the child or use its record, even if the syscall fails: fail
// closed.
inline bool SignalBoundaryOwner::detach(SignalBoundary &record,
                                        const SignalBoundaryPoint &point,
                                        SignalParentHandoff &handoff) {
  if (!matches(record, point) ||
      handoff.phase != SignalParentHandoff::Phase::Prepared ||
      handoff.owner != this || handoff.generation != live_generation ||
      handoff_generation != live_generation ||
      handoff.child_chain != point.child_chain ||
      handoff.parent_chain != point.parent_chain)
    return false;
  pop(record);
  handoff.phase = SignalParentHandoff::Phase::Detached;
  return true;
}

// On the trusted parent landing only. Success authorizes use of saved_parent
// for the one continuation; the caller must not replay it or copy this packet.
inline bool SignalBoundaryOwner::consume(SignalParentHandoff &handoff,
                                         const ThreadControl *current_thread,
                                         uint64_t current_chain,
                                         uint64_t expected_generation) {
  if (!valid() || current_thread != thread ||
      handoff.phase != SignalParentHandoff::Phase::Detached ||
      handoff.owner != this || !handoff_generation ||
      handoff.generation != handoff_generation ||
      expected_generation != handoff_generation ||
      current_chain != handoff.parent_chain ||
      current_chain != (live ? live->point.child_chain : base_chain))
    return false;
  handoff_generation = 0;
  handoff.phase = SignalParentHandoff::Phase::Consumed;
  return true;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
