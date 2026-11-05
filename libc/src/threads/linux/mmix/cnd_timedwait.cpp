//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/cnd_timedwait.h"
#include "src/pthread/linux/mmix/cancellation_condition.h"

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(CndVar) == sizeof(cnd_t) &&
              alignof(CndVar) == alignof(cnd_t));
static_assert(sizeof(Mutex) == sizeof(mtx_t) &&
              alignof(Mutex) == alignof(mtx_t));

LLVM_LIBC_FUNCTION(int, cnd_timedwait,
                   (cnd_t *__restrict cond, mtx_t *__restrict mutex,
                    const timespec *__restrict time_point)) {
  LIBC_CRASH_ON_NULLPTR(time_point);
  auto timeout = internal::AbsTimeout::from_timespec(*time_point, true);
  if (!timeout) {
    if (timeout.error() == internal::AbsTimeout::Error::Invalid)
      return thrd_error;
    timeout = internal::AbsTimeout::from_timespec({0, 0}, true);
  }
  int result =
      mmix::condition_wait_impl(reinterpret_cast<CndVar *>(cond),
                                reinterpret_cast<Mutex *>(mutex), *timeout);
  if (result == ETIMEDOUT)
    return thrd_timedout;
  return result == 0 ? thrd_success : thrd_error;
}

} // namespace LIBC_NAMESPACE_DECL
