//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/mtx_timedlock.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/threads/mutex.h"
#include "src/__support/time/abs_timeout.h"

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(Mutex) == sizeof(mtx_t) &&
              alignof(Mutex) == alignof(mtx_t));

LLVM_LIBC_FUNCTION(int, mtx_timedlock,
                   (mtx_t *__restrict storage,
                    const struct timespec *__restrict time_point)) {
  LIBC_CRASH_ON_NULLPTR(storage);
  auto *mutex = reinterpret_cast<Mutex *>(storage);
  MutexError result = mutex->try_lock();
  if (result != MutexError::BUSY)
    return result == MutexError::NONE ? thrd_success : thrd_error;

  LIBC_CRASH_ON_NULLPTR(time_point);
  auto timeout =
      internal::AbsTimeout::from_timespec(*time_point, /*realtime=*/true);
  if (!timeout)
    return timeout.error() == internal::AbsTimeout::Error::BeforeEpoch
               ? thrd_timedout
               : thrd_error;
  result = mutex->timed_lock(*timeout);
  if (result == MutexError::TIMEOUT)
    return thrd_timedout;
  return result == MutexError::NONE ? thrd_success : thrd_error;
}

} // namespace LIBC_NAMESPACE_DECL
