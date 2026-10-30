//===-- MMIX private jump cleanup stack -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_JUMP_CLEANUP_STACK_H
#define LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_JUMP_CLEANUP_STACK_H

#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Private, allocation-free cleanup, not general C++ stack unwinding. Storage is
// fixed per caller-owned node; unwind work is bounded by the live stack depth.
// The caller supplies a stable per-thread head (eventually TLS), initially
// null. All operations require blocked signals and the owning thread. Neither
// head nor linked nodes may be moved, copied, destroyed or otherwise modified.
//
// Hooks and their contexts must remain resident and valid until removal. Hooks
// must acquire the service gate to detach the actual observer, cannot allocate
// or call application code, and must never reenter this stack. The caller must
// not hold that gate. Success means no observer can still use the context.
// Checkpoints are live node identities, not lifetime/generation authentication:
// callers must not retain them across removal and reuse of their target node.
class JumpCleanupStack {
public:
  using Hook = bool (*)(void *);

  class alignas(8) Node {
    friend class JumpCleanupStack;
    Node **owner = nullptr;
    Node *next = nullptr;
    Hook hook = nullptr;
    void *context = nullptr;

  public:
    LIBC_INLINE constexpr Node() = default;
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    LIBC_INLINE ~Node() {
      // Destruction is never an implicit cleanup operation.
      if (linked())
        __builtin_trap();
    }
    LIBC_INLINE bool linked() const { return owner != nullptr; }
  };

  LIBC_INLINE static bool push(Node *&head, Node &node, Hook hook,
                               void *context) {
    if (!hook || node.linked() || (head && head->owner != &head))
      return false;
    node.owner = &head;
    node.next = head;
    node.hook = hook;
    node.context = context;
    head = &node;
    return true;
  }

  LIBC_INLINE static bool pop(Node *&head, Node &node) {
    if (head != &node || node.owner != &head)
      return false;
    // Keep the node reachable until detach succeeds. Continuing after failure
    // could let a jump discard storage still referenced by an observer.
    if (!node.hook(node.context))
      __builtin_trap();
    head = node.next;
    node.owner = nullptr;
    node.next = nullptr;
    node.hook = nullptr;
    node.context = nullptr;
    return true;
  }

  LIBC_INLINE static bool unwind(Node *&head, Node *target) {
    // Compare target only against live nodes; never dereference an unknown
    // target. Validate before invoking any hook or changing any link.
    Node *current = head;
    while (current != target) {
      if (!current || current->owner != &head)
        return false;
      current = current->next;
    }
    if (current && current->owner != &head)
      return false;
    while (head != target) {
      if (!pop(head, *head))
        __builtin_trap();
    }
    return true;
  }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_JUMP_CLEANUP_STACK_H
