//===-- MMIX Linux rwlock timeout helpers ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_RWLOCK_TIMEOUT_H
#define LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_RWLOCK_TIMEOUT_H

#include "hdr/errno_macros.h"
#include "hdr/time_macros.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/threads/unix_rwlock.h"
#include "src/__support/time/abs_timeout.h"
#include "src/__support/time/clock_gettime.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static_assert(sizeof(RwLock) == sizeof(pthread_rwlock_t) &&
              alignof(RwLock) == alignof(pthread_rwlock_t));

template <rwlock::Role role>
LIBC_INLINE int rwlock_clocklock(pthread_rwlock_t *storage, clockid_t clock,
                                 const timespec *abstime) {
  if (!storage || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC))
    return EINVAL;
  auto *rw = reinterpret_cast<RwLock *>(storage);
  auto initial =
      role == rwlock::Role::Reader ? rw->try_read_lock() : rw->try_write_lock();
  if (initial != RwLock::LockResult::Busy)
    return static_cast<int>(initial);
  LIBC_CRASH_ON_NULLPTR(abstime);
  // FIXME: Share this immediate-acquisition ordering when the common timed
  // wrappers no longer reject negative deadlines before attempting the lock.
  auto timeout =
      internal::AbsTimeout::from_timespec(*abstime, clock == CLOCK_REALTIME);
  if (!timeout) {
    if (timeout.error() == internal::AbsTimeout::Error::Invalid)
      return EINVAL;
    timeout =
        internal::AbsTimeout::from_timespec({0, 0}, clock == CLOCK_REALTIME);
  }
  const timespec limit = timeout->get_timespec();
  auto expired = [clock, limit]() {
    timespec now;
    // Both accepted clocks are mandatory on this platform, and storage is
    // local. A failed internal query is not a successful lock operation.
    if (!internal::clock_gettime(clock, &now).has_value())
      __builtin_trap();
    return now.tv_sec > limit.tv_sec ||
           (now.tv_sec == limit.tv_sec && now.tv_nsec >= limit.tv_nsec);
  };
  // Keep initial acquisition precedence. Once queued, expiry must withdraw
  // even if an interrupted futex wait observes a changed word and returns zero.
  auto result = role == rwlock::Role::Reader
                    ? rw->read_lock(*timeout, LIBC_COPT_RWLOCK_DEFAULT_SPIN_COUNT,
                                    expired)
                    : rw->write_lock(*timeout, LIBC_COPT_RWLOCK_DEFAULT_SPIN_COUNT,
                                     expired);
  return static_cast<int>(result);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_RWLOCK_TIMEOUT_H
