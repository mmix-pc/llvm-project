//===-- Private bounded AIO wait observers ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_WAIT_SET_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_WAIT_SET_H

#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/aio_request.h"

namespace LIBC_NAMESPACE_DECL {

// Private observation only: no waiting, wakeups, syscalls, locks, allocation or
// application callbacks. This is not a public aio_suspend/lio_listio provider
// or a signal-safety guarantee. The service serializes ALL access, including
// queries, with request completion, result consumption and binding changes.
//
// Supply exclusive, default-constructed Slot storage prepared for the unique
// members of this group before entering the service gate. This bounds only
// this group's references, NOT the process's retained records or list length.
// Signal-safe any-completion waiting uses AioSuspendObservers, not allocation
// of this storage or a variable-length array on the handler's stack.
// Keep the set, slots and requests at stable addresses until detach(). Like
// AioRequest::Reference, destruction does not release ownership automatically.
class AioWaitSet {
public:
  class Slot {
    friend class AioWaitSet;
    AioRequest *request = nullptr;
    AioRequest::Reference reference;

  public:
    LIBC_INLINE constexpr Slot() = default;
    Slot(const Slot &) = delete;
    Slot &operator=(const Slot &) = delete;
  };

private:
  Slot *const slots;
  const size_t capacity;
  size_t count = 0;
  bool active = false;

  LIBC_INLINE ErrorOr<bool> ready(bool all) const {
    if (!active)
      return Error(EINVAL);
    for (size_t i = 0; i < count; ++i) {
      auto completion = result(i);
      if (!completion && completion.error() != EINPROGRESS)
        return Error(completion.error());
      if (all && !completion)
        return false;
      if (!all && completion)
        return true;
    }
    return all;
  }

public:
  LIBC_INLINE constexpr AioWaitSet(Slot *storage, size_t prepared_slot_count)
      : slots(storage), capacity(prepared_slot_count) {}
  AioWaitSet(const AioWaitSet &) = delete;
  AioWaitSet &operator=(const AioWaitSet &) = delete;

  // resolve(control_block) returns a trusted AioRequest *, or nullptr for an
  // invalid binding. It must validate process/binding identity using service
  // storage, never an untrusted aiocb private pointer. This internal lookup
  // must not reenter, change bindings, allocate, lock, make syscalls or invoke
  // application callbacks. The caller validates list readability and signed
  // public nent values before entry. Neither the list nor resolver is saved.
  //
  // Nulls are ignored; trusted request pointers are deduplicated in first-seen
  // order. EINVAL (invalid binding/request/storage) or EAGAIN (slot/holder
  // exhaustion) rolls back every reference acquired by this attempt. EBUSY
  // leaves an already attached set unchanged, even if that set is empty.
  template <class ControlBlock, class Resolver>
  LIBC_INLINE ErrorOr<void> attach(const ControlBlock *const *list, size_t nent,
                                   Resolver &&resolve) {
    if (active)
      return Error(EBUSY);
    if ((!list && nent != 0) || (!slots && capacity != 0))
      return Error(EINVAL);
    for (size_t i = 0; i < nent; ++i) {
      if (!list[i])
        continue;
      AioRequest *request = resolve(list[i]);
      if (!request) {
        detach();
        return Error(EINVAL);
      }
      size_t j = 0;
      while (j < count && slots[j].request != request)
        ++j;
      if (j != count)
        continue;
      if (count == capacity) {
        detach();
        return Error(EAGAIN);
      }
      auto retained = request->retain(slots[count].reference);
      if (!retained) {
        detach();
        return Error(retained.error());
      }
      slots[count++].request = request;
    }
    active = true;
    return {};
  }

  LIBC_INLINE bool attached() const { return active; }
  LIBC_INLINE size_t size() const { return count; }

  // Empty/all-null attachment is explicit: any_complete() is false and
  // all_complete() is true. Public empty-list policy belongs to the caller;
  // an empty any-wait must not be reported as a completed operation.
  // Operation errors (including ECANCELED) are completions, not query errors.
  LIBC_INLINE ErrorOr<bool> any_complete() const { return ready(false); }
  LIBC_INLINE ErrorOr<bool> all_complete() const { return ready(true); }

  // Indexes refer to unique requests in first-seen order, not list positions.
  // Pending returns EINPROGRESS; detached/out-of-range returns EINVAL. The
  // retained result survives aio_return() and reuse of the aiocb address.
  LIBC_INLINE ErrorOr<AioRequest::Result> result(size_t index) const {
    if (!active || index >= count)
      return Error(EINVAL);
    return slots[index].request->result(slots[index].reference);
  }

  // Idempotent, including after a failed attach. Release only this observer's
  // references, never original I/O, application results or notifications.
  LIBC_INLINE void detach() {
    while (count != 0) {
      Slot &slot = slots[--count];
      slot.request->release(slot.reference);
      slot.request = nullptr;
    }
    active = false;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_WAIT_SET_H
