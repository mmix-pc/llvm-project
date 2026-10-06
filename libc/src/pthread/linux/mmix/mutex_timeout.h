//===-- MMIX Linux mutex timeout helpers -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_MUTEX_TIMEOUT_H
#define LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_MUTEX_TIMEOUT_H

#include "hdr/errno_macros.h"
#include "hdr/time_macros.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/threads/mutex.h"
#include "src/__support/time/abs_timeout.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

LIBC_INLINE int mutex_wait_result(MutexError result) {
  switch (result) {
  case MutexError::NONE:
    return 0;
  case MutexError::DEADLOCK:
    return EDEADLK;
  case MutexError::OVERFLOW:
    return EAGAIN;
  case MutexError::TIMEOUT:
    return ETIMEDOUT;
  default:
    return EINVAL;
  }
}

LIBC_INLINE int mutex_clocklock(pthread_mutex_t *storage, clockid_t clock,
                                const timespec *abstime) {
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
    return EINVAL;
  LIBC_CRASH_ON_NULLPTR(storage);
  auto *mutex = reinterpret_cast<Mutex *>(storage);
  // Immediate acquisition (including recursive ownership) ignores the deadline.
  MutexError initial = mutex->try_lock();
  if (initial != MutexError::BUSY)
    return mutex_wait_result(initial);
  LIBC_CRASH_ON_NULLPTR(abstime);
  auto timeout =
      internal::AbsTimeout::from_timespec(*abstime, clock == CLOCK_REALTIME);
  if (!timeout)
    return timeout.error() == internal::AbsTimeout::Error::Invalid ? EINVAL
                                                                   : ETIMEDOUT;
  // The common futex wait retries EINTR with this same absolute deadline.
  return mutex_wait_result(mutex->timed_lock(*timeout));
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_MUTEX_TIMEOUT_H
