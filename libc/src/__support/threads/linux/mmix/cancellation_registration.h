//===-- MMIX cancellation descriptor lifetime -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_REGISTRATION_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_REGISTRATION_H

#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/new.h"
#include "src/setjmp/mmix/linux/jump_cleanup_stack.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct CancellationPoint;

// Owner-thread operations with all signals blocked. Publish before saving the
// private recovery context, so its watermark retains this registration. Public
// jumps to older frames and forced cleanup retire it before abandoning storage.
// Trivial destruction is intentional: a legal longjmp cannot depend on C++
// automatic destruction. The wrapper supplies an explicit unwind cleanup.
class CancellationRegistration {
  using Node = JumpCleanupStack::Node;
  cpp::Atomic<CancellationPoint *> &publication;
  Node *&head;
  CancellationPoint *const point;
  CancellationPoint *const previous;
  alignas(Node) unsigned char storage[sizeof(Node)];
  Node *node = nullptr;
  bool attempted = false;
  // The unwind cleanup can inspect this flag before masking signals.
  cpp::Atomic<bool> active{false};

  static bool retire(void *opaque) {
    auto &self = *static_cast<CancellationRegistration *>(opaque);
    if (!self.is_active() || self.publication.load(cpp::MemoryOrder::ACQUIRE) !=
                            self.point)
      return false;
    self.publication.store(self.previous, cpp::MemoryOrder::RELEASE);
    self.active.store(false, cpp::MemoryOrder::RELAXED);
    return true;
  }

public:
  CancellationRegistration(cpp::Atomic<CancellationPoint *> &publication,
                           Node *&head, CancellationPoint *point,
                           CancellationPoint *previous)
      : publication(publication), head(head), point(point), previous(previous) {}
  CancellationRegistration(const CancellationRegistration &) = delete;
  CancellationRegistration &operator=(const CancellationRegistration &) = delete;

  bool begin() {
    if (attempted)
      return false;
    attempted = true;
    if (!point || point == previous ||
        publication.load(cpp::MemoryOrder::ACQUIRE) != previous)
      return false;
    node = new (storage) Node;
    if (!JumpCleanupStack::push(head, *node, retire, this)) {
      node->~Node();
      return false;
    }
    publication.store(point, cpp::MemoryOrder::RELEASE);
    active.store(true, cpp::MemoryOrder::RELAXED);
    return true;
  }

  bool is_active() { return active.load(cpp::MemoryOrder::RELAXED); }

  bool finish() {
    if (!is_active())
      return true;
    if (!JumpCleanupStack::pop(head, *node))
      return false;
    node->~Node();
    return true;
  }
};
static_assert(__is_trivially_destructible(CancellationRegistration));
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
