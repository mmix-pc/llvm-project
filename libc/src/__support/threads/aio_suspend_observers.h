//===-- Allocation-free AIO completion observers ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_SUSPEND_OBSERVERS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_SUSPEND_OBSERVERS_H

#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// One fixed-size registration per any-completion waiter, not per request.
// ALL operations, including ready(), require the same signal-safe service
// gate. Lists must remain valid and unchanged until detach(), including during
// signal handling. No allocation, request/result ownership or blocking here.
//
// Notify every original completion under that gate BEFORE unbinding/retiring
// its request. Thus address reuse cannot replace an unnoticed completion: the
// old observer is already latched. No subsequent lookup or result access is
// needed. Prepared all-completion/result-owning groups use AioWaitSet instead.
template <class ControlBlock> class AioSuspendObservers {
public:
  using Wake = void (*)(void *);
  class Observer {
    friend class AioSuspendObservers;
    AioSuspendObservers *owner = nullptr;
    Observer *next = nullptr;
    const ControlBlock *const *list = nullptr;
    size_t count = 0;
    Wake wake = nullptr;
    void *context = nullptr;
    bool completed = false;

  public:
    LIBC_INLINE constexpr Observer() = default;
    Observer(const Observer &) = delete;
    Observer &operator=(const Observer &) = delete;
    LIBC_INLINE ~Observer() {
      if (owner)
        __builtin_trap();
    }
    LIBC_INLINE bool attached() const { return owner != nullptr; }
    LIBC_INLINE bool ready() const { return owner && completed; }
  };

private:
  Observer *head = nullptr;

public:
  LIBC_INLINE constexpr AioSuspendObservers() = default;
  AioSuspendObservers(const AioSuspendObservers &) = delete;
  AioSuspendObservers &operator=(const AioSuspendObservers &) = delete;
  LIBC_INLINE ~AioSuspendObservers() {
    if (head)
      __builtin_trap();
  }

  // Query returns ErrorOr<bool>: true means an authenticated bound request
  // already completed, including I/O error or cancellation. It must not mutate
  // bindings, allocate, reenter or release the gate. Validate the entire list
  // before publication; failure leaves the observer reusable and unattached.
  // Wake is an internal nonblocking signal-safe primitive (e.g. atomic store
  // and futex wake), NEVER application code. It runs under the gate and may
  // not reenter/detach. This keeps its context alive through the final wake.
  // Wake must retain the ready state if no consumer sleeps yet and must handle
  // primitive errors explicitly rather than losing a registered notification.
  template <class Query>
  LIBC_INLINE ErrorOr<void>
  attach(Observer &observer, const ControlBlock *const *list, size_t count,
         Query &&query, Wake wake, void *context) {
    if (observer.owner)
      return Error(EBUSY);
    if ((!list && count) || !wake)
      return Error(EINVAL);
    bool completed = false;
    for (size_t i = 0; i < count; ++i) {
      if (!list[i])
        continue;
      auto result = query(list[i]);
      if (!result)
        return Error(result.error());
      completed |= *result;
    }
    observer.list = list;
    observer.count = count;
    observer.wake = wake;
    observer.context = context;
    observer.completed = completed;
    observer.owner = this;
    observer.next = head;
    head = &observer;
    // The caller checks ready() before sleeping; no wake is needed for an
    // already completed list. Empty/all-null lists never invent completion.
    return {};
  }

  // Only for authenticated original completions, not cancellation-control
  // acknowledgements. Duplicate notifications do not repeat wake or reset it.
  LIBC_INLINE void complete(const void *control_block) {
    if (!control_block)
      return;
    for (Observer *observer = head; observer; observer = observer->next) {
      if (observer->completed)
        continue;
      for (size_t i = 0; i < observer->count; ++i) {
        if (observer->list[i] != control_block)
          continue;
        observer->completed = true;
        observer->wake(observer->context);
        break;
      }
    }
  }

  // Detach on success, timeout, interruption and cancellation before list or
  // wake-context lifetime ends. Do not wait under the gate for the consumer.
  LIBC_INLINE bool detach(Observer &observer) {
    if (observer.owner != this)
      return false;
    Observer **link = &head;
    while (*link != &observer)
      link = &(*link)->next;
    *link = observer.next;
    observer.owner = nullptr;
    observer.next = nullptr;
    observer.list = nullptr;
    observer.count = 0;
    observer.wake = nullptr;
    observer.context = nullptr;
    observer.completed = false;
    return true;
  }
};

} // namespace LIBC_NAMESPACE_DECL
#endif
