//===-- Stack-owned cleanup callback leases ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CLEANUP_OWNERSHIP_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CLEANUP_OWNERSHIP_H

#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Owner-thread storage only. Callers block signals around every operation,
// including Invocation destruction, and keep this owner alive through all
// nodes and invocations. A Node belongs to the lexical cleanup frame, never
// to a push helper's frame. Record/frame validation remains the caller's job.
//
// This does not admit signal unwinding or suppress cancellation. Before
// restoring signals or cancellation, callers must publish both cleanup lists
// consistently and protect detached invocation lifetimes. Nonlocal transfers
// must explicitly release abandoned invocations; C++ destruction is not a
// longjmp hook. No allocation, loader lock or cancellation hook is used here.
class CleanupOwners {
public:
  class Node {
    friend class CleanupOwners;
    const void *record = nullptr;
    uint64_t serial = 0;
    Node *previous = nullptr;
    loader::LoaderSignalLease lease{};

  public:
    constexpr Node() = default;
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    bool linked() const { return record != nullptr; }
  };

  class Invocation {
    friend class CleanupOwners;
    CleanupOwners *owner = nullptr;
    const void *record = nullptr;
    uint64_t serial = 0;
    loader::LoaderSignalLease lease{};

  public:
    constexpr Invocation() = default;
    Invocation(const Invocation &) = delete;
    Invocation &operator=(const Invocation &) = delete;
    ~Invocation() { release(); }
    bool active() const { return owner != nullptr; }
    // Idempotent for explicit completion followed by scope destruction.
    void release();
  };

  // Non-owning observation, never a second lease. Only a process snapshot may
  // copy the actual Invocation together with its owner and loader accounting.
  // In-process copies of this witness confer no release/restore authority.
  struct Witness {
    const CleanupOwners *owner = nullptr;
    const Invocation *invocation = nullptr;
    const void *record = nullptr;
    uint64_t serial = 0;
    const Node *remaining = nullptr;
    uint64_t invocations = 0;
    loader::LoaderSignalLease lease{};
  };

  // Pending-only identity: no Invocation and no borrowed or synthetic lease.
  struct PendingWitness {
    const CleanupOwners *owner = nullptr;
    const Node *remaining = nullptr;
  };

private:
  Node *head = nullptr;
  uint64_t invocations = 0;

  static bool empty(const loader::LoaderSignalLease &lease) {
    return !lease.snapshot && !lease.images[0] && !lease.images[1];
  }

  void finish(Invocation &invocation) {
    if (invocation.owner != this || !invocations)
      __builtin_trap();
    // The service drops image references before their protecting snapshot.
    // Never commit a permanent signal-registration pin for cleanup code.
    finish_shared_signal(&invocation.lease, false);
    if (!empty(invocation.lease))
      __builtin_trap();
    invocation.owner = nullptr;
    invocation.record = nullptr;
    invocation.serial = 0;
    --invocations;
  }

public:
  constexpr CleanupOwners() = default;
  CleanupOwners(const CleanupOwners &) = delete;
  CleanupOwners &operator=(const CleanupOwners &) = delete;

  bool push(Node &node, const void *record, uint64_t serial,
            uintptr_t callback) {
    if (!record || !serial || !callback || node.linked() ||
        !empty(node.lease))
      return false;
    for (auto *entry = head; entry; entry = entry->previous)
      if (entry->record == record)
        return false;

    // The service rolls back partial acquisition and leaves an empty output
    // on failure. Nothing in the owner stack is published until it succeeds.
    if (retain_shared_signal(callback, 0, &node.lease)) {
      if (!empty(node.lease))
        __builtin_trap();
      return false;
    }
    if (!node.lease.snapshot || !node.lease.images[0])
      __builtin_trap();
    node.record = record;
    node.serial = serial;
    node.previous = head;
    head = &node;
    return true;
  }

  // Transfer, not release: callback code stays owned after its record has
  // been consumed. The cleared Node may be reused while this invocation lives.
  bool detach(Node &node, const void *record, uint64_t serial,
              Invocation &invocation) {
    if (head != &node || !record || node.record != record ||
        !serial || node.serial != serial || invocation.active() ||
        !empty(invocation.lease) || invocations == ~uint64_t(0))
      return false;
    invocation.owner = this;
    invocation.record = record;
    invocation.serial = serial;
    invocation.lease = node.lease;
    ++invocations;
    head = node.previous;
    node.lease = {};
    node.record = nullptr;
    node.serial = 0;
    node.previous = nullptr;
    return true;
  }

  bool has_records() const { return head != nullptr; }
  bool detach(const void *record, uint64_t serial, Invocation &invocation) {
    return head && detach(*head, record, serial, invocation);
  }
  bool has_invocations() const { return invocations != 0; }

  // A pending cursor is available only after positively excluding detached
  // invocations, even when the registration list itself is empty.
  bool snapshot_pending(PendingWitness &out) const {
    if (has_invocations())
      return false;
    out = {this, head};
    return true;
  }

  static bool same_pending_witness(const PendingWitness &a,
                                   const PendingWitness &b) {
    return a.owner == b.owner && a.remaining == b.remaining;
  }

  // Read-only owner-thread inspection under signal exclusion. The live
  // Invocation is supplied independently, not dereferenced from a witness.
  bool snapshot(const Invocation &invocation, Witness &out) const {
    if (invocation.owner != this || !invocations || !invocation.record ||
        !invocation.serial || !invocation.lease.snapshot ||
        !invocation.lease.images[0])
      return false;
    out = {this, &invocation, invocation.record, invocation.serial, head,
           invocations, invocation.lease};
    return true;
  }

  // Nested registration changes the list/count, but not a detached lease's
  // identity. Compare fresh observations, never dereference saved pointers.
  static bool same_invocation(const Witness &a, const Witness &b) {
    return a.owner == b.owner && a.invocation == b.invocation &&
           a.record == b.record && a.serial == b.serial &&
           a.lease.snapshot == b.lease.snapshot &&
           a.lease.images[0] == b.lease.images[0] &&
           a.lease.images[1] == b.lease.images[1];
  }

  static bool same_witness(const Witness &a, const Witness &b) {
    return same_invocation(a, b) && a.remaining == b.remaining &&
           a.invocations == b.invocations;
  }

  bool validate(const Invocation &invocation, const Witness &witness) const {
    Witness now;
    return snapshot(invocation, now) && same_witness(now, witness);
  }

  // Walk only a cursor obtained from a successful live Witness/PendingWitness
  // snapshot, never from a saved witness supplied by the caller. This
  // advances the observation, never the owning list or any lease.
  static bool match_record(const Node *&cursor, const void *record,
                           uint64_t serial) {
    if (!cursor || !record || !serial || cursor->record != record ||
        cursor->serial != serial || !cursor->lease.snapshot ||
        !cursor->lease.images[0])
      return false;
    cursor = cursor->previous;
    return true;
  }
  // An empty registration list alone is not a terminal-transfer certificate.
  bool idle() const { return !has_records() && !has_invocations(); }
};

inline void CleanupOwners::Invocation::release() {
  if (owner)
    owner->finish(*this);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif
