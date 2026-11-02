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
