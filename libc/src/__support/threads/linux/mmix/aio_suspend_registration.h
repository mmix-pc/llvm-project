//===-- MMIX AIO suspend registration --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SUSPEND_REGISTRATION_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SUSPEND_REGISTRATION_H

#include "aio_gate.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/aio_wait_event.h"
#include "src/__support/threads/aio_suspend_observers.h"
#include "src/setjmp/mmix/linux/jump_cleanup_stack.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Single-use, caller-stack registration, not a public aio_suspend provider.
// The service and gate must outlive finish()/jump cleanup. The immutable list
// is borrowed until then. The caller supplies THIS thread's private cleanup
// head; copied fork-child service state must never be entered through here.
//
// Deliberately trivially destructible: legal longjmp may discard the wrapper's
// automatic storage. Construct nontrivial intrusive nodes in raw storage and
// detach explicitly on every normal/error/cancellation path, or by the paired
// MMIX jump protocol. A C++ RAII wait guard alone cannot implement this rule.
// Calling code must likewise avoid automatic nontrivial objects across waits
// that an application longjmp may leave. This does not enable handler cancel.
template <class ControlBlock, class Gate = AioGate>
class AioSuspendRegistration {
  using Registry = AioSuspendObservers<ControlBlock>;
  using Observer = typename Registry::Observer;
  using Node = JumpCleanupStack::Node;
  Registry &registry;
  Gate &gate;
  Node *&head;
  alignas(Observer) unsigned char observer_storage[sizeof(Observer)];
  alignas(Node) unsigned char node_storage[sizeof(Node)];
  Observer *observer = nullptr;
  Node *node = nullptr;
  AioWaitEvent event;
  bool attempted = false;
  bool active = false;

  static int block(uint64_t &previous) {
    const uint64_t all = UINT64_MAX;
    long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &all,
                                     &previous, sizeof(previous));
    return result < 0 ? int(-result) : 0;
  }
  static void restore(uint64_t previous) {
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &previous, nullptr,
                           sizeof(previous)))
      __builtin_trap();
  }
  static void wake(void *context) {
    static_cast<AioSuspendRegistration *>(context)->event.publish();
  }
  static bool detach(void *context) {
    auto &self = *static_cast<AioSuspendRegistration *>(context);
    typename Gate::Guard guard(self.gate);
    if (!guard || !self.active || !self.registry.detach(*self.observer))
      return false;
    self.active = false;
    return true;
  }
  template <class Query>
  int attach_masked(const ControlBlock *const *list, size_t count,
                    Query &query) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return guard.error();
    auto result = registry.attach(*observer, list, count, query, wake, this);
    if (!result)
      return result.error();
    active = true;
    if (observer->ready())
      event.publish();
    if (!JumpCleanupStack::push(head, *node, detach, this))
      __builtin_trap();
    return 0;
  }

public:
  AioSuspendRegistration(Registry &registry, Gate &gate, Node *&head)
      : registry(registry), gate(gate), head(head) {}
  AioSuspendRegistration(const AioSuspendRegistration &) = delete;
  AioSuspendRegistration &operator=(const AioSuspendRegistration &) = delete;

  bool belongs_to(const Registry &owner, const Gate &state_gate) const {
    return &registry == &owner && &gate == &state_gate;
  }

  // Query is the service's authenticated lookup under the gate; it must not
  // throw, allocate or call application code. Positive errno, no errno writes.
  template <class Query>
  int begin(const ControlBlock *const *list, size_t count, Query query) {
    static_assert(__is_trivially_destructible(Query),
                  "A handler jump must not skip the query destructor");
    uint64_t previous;
    if (int error = block(previous))
      return error;
    int error = EBUSY;
    if (!attempted) {
      attempted = true;
      observer = new (observer_storage) Observer;
      node = new (node_storage) Node;
      error = attach_masked(list, count, query);
      if (error) {
        observer->~Observer();
        node->~Node();
      }
    }
    // No nontrivial local destructor remains when pending handlers run here.
    restore(previous);
    return error;
  }

  // Call only on the owning thread, in LIFO order. No service gate held. An
  // error leaves ownership intact: the wrapper cannot discard its stack; if
  // it cannot retry safely it must terminate, never return a dangling observer.
  int finish() {
    uint64_t previous;
    if (int error = block(previous))
      return error;
    int error = EINVAL;
    if (active) {
      if (head != node)
        error = EBUSY;
      else {
        if (!JumpCleanupStack::pop(head, *node))
          __builtin_trap();
        observer->~Observer();
        node->~Node();
        error = 0;
      }
    }
    restore(previous);
    return error;
  }

  // Event waiting never takes the gate. The service must detach this
  // registration before releasing the event's stack storage.
  AioWaitEvent &completion() { return event; }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
