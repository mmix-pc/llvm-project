//===-- MMIX precreated AIO notification ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_NOTIFICATION_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_NOTIFICATION_H

#include "hdr/types/siginfo_t.h"
#include "notification_callback.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// One single-use notification owner per request or list group. The service
// bounds request admission (including pending cleanup), serializes all access,
// suppresses caller cancellation, and retains this
// object through cleanup. There is at most one envelope and waiter per owner,
// with no internal queue. Releasing this slot does not refund a live-thread
// budget. No registry lock may be held on entry. This is not an
// async-signal-safe API.
//
// The service must exclude fork/exec/exit transitions while
// preparing/activating, discard old-image notifications at teardown, and never
// use an inherited owner in a fork child. Application stack/value pointees
// remain application-owned.
class AioNotification {
  enum class State { Fresh, Prepared, Terminal };
  State state = State::Fresh;
  int mode = 0;
  siginfo_t signal_info{};
  NotificationCallback *callback = nullptr;
  ThreadControl *waiter = nullptr;
  ThreadResources retained{};

public:
  AioNotification() = default;
  ~AioNotification();
  AioNotification(const AioNotification &) = delete;
  AioNotification &operator=(const AioNotification &) = delete;

  // All errors are positive errno, without changing errno. Exactly one attempt
  // is allowed. NONE ignores the other fields; SIGNAL copies signo/value and
  // captures sender identity; THREAD copies callback/attributes via the managed
  // envelope and precreates a dormant waiter before returning success.
  // Main must already be registered. Only success permits request acceptance.
  // Failure is terminal, never runs application code, and may require
  // cleanup().
  long prepare(const sigevent &, const TLSImage &, uintptr_t page_size);

  // Call only after publishing the original operation's terminal result,
  // including ECANCELED, never merely on a successful cancel-control CQE.
  // No allocation, callback creation, resource release, or wait for application
  // code. THREAD EBUSY leaves the waiter prepared for retry (registry admission
  // refused). SIGNAL queues SI_ASYNCIO once; EAGAIN retains the copied event
  // for a later iteration, without altering the completed I/O result. Other
  // delivery errors are terminal and must be handled by the service.
  // SIGNAL also returns EBUSY after process-exit election, retaining the event
  // for coordinated teardown. Admission is a lock-free check per attempt: one
  // already admitted may finish, but neither a new attempt nor a retry may
  // start after observing exit. This does not wait for or revoke queued signals.
  // Duplicate successful attempts return EALREADY.
  long activate();

  // Reject prepared admission, or suppress notification during coordinated
  // teardown. Wakes a dormant thread only to exit, never to invoke its
  // callback. Does not undo activation. Repeated discard of terminal state is
  // harmless.
  void discard();

  // Failed registration must return a borrowed stack only after its dormant
  // waiter has exited. Call outside service/registry locks, with application
  // signals blocked and cancellation suppressed. Independent cleanup can still
  // be retried later; successful or activated notifications are not withdrawn.
  void withdraw_failed_admission();

  // EBUSY while prepared. Otherwise release independent preparation rollback
  // resources and the envelope reference; failed releases remain owned for
  // retry. Never waits for or reclaims a running/discarded callback thread.
  long cleanup();

  // Only this service record's obligations, not the callback thread's lifetime.
  // A live thread retains its own envelope/DSO reference and registry budget
  // through real terminal teardown/reaping, including exit and cancellation.
  // Keep this object's address stable until drained().
  bool drained() {
    return state != State::Prepared && !waiter && !callback &&
           !retained.owns_mappings();
  }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
