//===-- MMIX Linux implementation of pthread_mutex_timedlock --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_mutex_timedlock.h"
#include "mutex_timeout.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutex_timedlock,
                   (pthread_mutex_t *__restrict mutex,
                    const timespec *__restrict abstime)) {
  return mmix::mutex_clocklock(mutex, CLOCK_REALTIME, abstime);
}
} // namespace LIBC_NAMESPACE_DECL
