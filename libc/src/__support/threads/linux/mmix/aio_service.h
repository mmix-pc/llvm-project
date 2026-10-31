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

// Called with application/cancellation signals excluded by ForkOperation,
// before allocator/loader/registry snapshot locks. Every successful prepare
// needs exactly one finish, including failed clone rollback. A copied active
// service is quarantined, never drained by the child.
int prepare_aio_fork(bool minimal, bool *drained);
void finish_aio_fork(bool child);
void close_aio_admission();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
