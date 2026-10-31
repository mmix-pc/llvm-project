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
