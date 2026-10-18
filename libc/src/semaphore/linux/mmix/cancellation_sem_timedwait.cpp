//===-- MMIX Linux cancelable sem_timedwait
//--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cancellation_semaphore.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/semaphore/sem_timedwait.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sem_timedwait,
                   (sem_t *__restrict sem,
                    const timespec *__restrict abstime)) {
  // POSIX requires a cancellation check even when acquisition cannot block.
  // Never check again after acquiring: the token then belongs to the caller.
  mmix::test_cancel();
  LIBC_CRASH_ON_NULLPTR(sem);
  auto *semaphore = reinterpret_cast<Semaphore *>(sem);
  int error = semaphore->is_valid()
                  ? semaphore->clockwait_with(CLOCK_REALTIME, abstime,
                                              mmix::semaphore_wait)
                  : EINVAL;
  if (error) {
    libc_errno = error;
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
