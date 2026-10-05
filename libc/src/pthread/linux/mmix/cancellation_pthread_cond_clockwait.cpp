//===-- MMIX Linux cancelable clock condition wait ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cancellation_condition.h"
#include "src/pthread/pthread_cond_clockwait.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_cond_clockwait,
                   (pthread_cond_t *__restrict cond,
                    pthread_mutex_t *__restrict mutex, clockid_t clock,
                    const timespec *__restrict abstime)) {
  if (!pthread_cond_utils::is_supported_clock(clock))
    return EINVAL;
  return mmix::condition_timed_wait(
      pthread_cond_utils::to_cndvar(cond), pthread_cond_utils::to_mutex(mutex),
      abstime, pthread_cond_utils::is_realtime_clock(clock));
}
} // namespace LIBC_NAMESPACE_DECL
