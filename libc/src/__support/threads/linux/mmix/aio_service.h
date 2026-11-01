//===-- MMIX process AIO service -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SERVICE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SERVICE_H

#include "aio_input.h"
#include "src/__support/OSUtil/linux/aio_wait_event.h"
#include "src/__support/error_or.h"
#include "src/__support/threads/aio_request.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Private integration surface, not exported AIO entrypoints. Submission runs
// only on an admitted ordinary thread; queries allocate nothing and never
// initialize a service. Errors are positive internal codes; eventual public
// wrappers own errno translation. No aiocb private pointer is trusted or updated.
int submit_aio_transfer(const AioInput &);
int submit_aio_sync(const AioSyncInput &);
ErrorOr<int> query_aio_error(const aiocb *);
ErrorOr<AioRequest::Result> take_aio_result(const aiocb *);
// Ordinary-thread transaction. A null key snapshots this numeric descriptor;
// a nonnull key selects its existing binding. The result is an AIO_* status,
// not a cancel-control CQE. Result retrieval remains independently owned.
ErrorOr<int> cancel_aio_requests(int descriptor, const aiocb *);
// Private nonwaiting list transaction. Preserves accepted siblings on member
// failure and releases caller ownership, not the group's notification owner.
int submit_aio_list_nowait(aiocb *const *, int count, const sigevent *);
// Ordinary-thread list wait. Never reads the supplied group event; only member
// notifications survive. Full completion reports the aggregate list result,
// without consuming member results. Interruption/cancellation/jump releases
// only the group caller owner. The optional adapter is resident runtime/test
// code, not an application callback. Handler cancellation is not admitted.
int submit_aio_list_wait(aiocb *const *, int count, const sigevent *,
                         AioWaitEvent::WaitSyscall = nullptr, void * = nullptr);
// Private absolute CLOCK_MONOTONIC wait. No lazy service creation. The optional
// syscall adapter is resident runtime/test code, not an application callback.
// Handler cancellation remains outside this integration boundary.
int suspend_aio_requests(const aiocb *const *, size_t count,
                         const timespec *deadline,
                         AioWaitEvent::WaitSyscall = nullptr, void * = nullptr);
// Same registration, ready-first and cleanup rules, with a relative interval.
// Null is unbounded. Negative seconds or invalid nanoseconds yield EINVAL;
// an unrepresentable absolute deadline yields private ERANGE, not a selected
// public aio_suspend overflow policy. Empty/all-null lists retain the absolute
// wait's behavior. This does not add a ready-path cancellation point.
int suspend_aio_requests_relative(const aiocb *const *, size_t count,
                                  const timespec *timeout,
                                  AioWaitEvent::WaitSyscall = nullptr,
                                  void * = nullptr);

namespace aio_service_detail {
// Only called with an already registered, cleanup-protected event. Clock is
// resident nonallocating code returning a positive error, never an application
// callback. Keeping this adapter separate permits native arithmetic injection
// without admitting a clock hook through the service interface.
template <class Clock>
LIBC_INLINE int wait_relative(AioWaitEvent &event, const timespec *timeout,
                              Clock clock, AioWaitEvent::WaitSyscall wait,
                              void *context) {
  static_assert(__is_trivially_destructible(Clock));
  if (event.ready())
    return 0;
  if (!timeout)
    return event.wait(nullptr, wait, context);

  const timespec interval = *timeout;
  if (interval.tv_sec < 0 || interval.tv_nsec < 0 ||
      interval.tv_nsec >= 1000000000)
    return event.ready() ? 0 : EINVAL;

  timespec now{};
  if (int error = clock(now))
    return event.ready() ? 0 : error;
  if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000)
    return event.ready() ? 0 : EPROTO;

  // Both nanosecond fields are normalized, so their sum fits even a 32-bit
  // long. Check seconds and the normalization carry separately before use.
  timespec deadline{};
  deadline.tv_nsec = now.tv_nsec + interval.tv_nsec;
  const bool carry = deadline.tv_nsec >= 1000000000;
  if (__builtin_add_overflow(now.tv_sec, interval.tv_sec, &deadline.tv_sec) ||
      __builtin_add_overflow(deadline.tv_sec, carry, &deadline.tv_sec))
    return event.ready() ? 0 : ERANGE;
  if (carry)
    deadline.tv_nsec -= 1000000000;
  // The absolute wait copies this once; spurious wakes never resample Clock.
  return event.wait(&deadline, wait, context);
}
} // namespace aio_service_detail

// Called with application/cancellation signals excluded by ForkOperation,
// before allocator/loader/registry snapshot locks. Early prepare closes only
// admission, except that a drained service may retire its helpers. Final freeze
// is nonblocking and runs after all other locks/census. Thaw precedes ordinary
// restoration; finish reopens admission afterwards. Every successful prepare
// needs exactly one finish, including rollback before freeze or failed clone.
// A copied active service is quarantined, never drained by the child.
int prepare_aio_fork(bool minimal, bool *drained);
int freeze_aio_fork();
void thaw_aio_fork(bool child);
void finish_aio_fork(bool child);
void close_aio_admission();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
