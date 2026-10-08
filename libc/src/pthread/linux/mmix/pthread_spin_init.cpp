//===-- MMIX pthread_spin_init implementation -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_spin_init.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/common.h"
#include "src/__support/threads/spin_lock.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(pthread_spinlock_t::__lockword) == sizeof(SpinLock) &&
              alignof(decltype(pthread_spinlock_t::__lockword)) ==
                  alignof(SpinLock));

LLVM_LIBC_FUNCTION(int, pthread_spin_init,
                   (pthread_spinlock_t * lock, int pshared)) {
  if (!lock ||
      (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED))
    return EINVAL;
#ifndef LIBC_MMIX_PROCESS_SHARED
  if (pshared == PTHREAD_PROCESS_SHARED)
    return ENOTSUP;
#endif
  ::new (&lock->__lockword) SpinLock();
  cpp::AtomicRef(lock->__owner).store(0, cpp::MemoryOrder::RELAXED);
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
