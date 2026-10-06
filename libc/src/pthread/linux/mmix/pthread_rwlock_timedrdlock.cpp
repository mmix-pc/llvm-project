//===-- MMIX pthread_rwlock_timedrdlock implementation ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_rwlock_timedrdlock.h"
#include "rwlock_timeout.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_rwlock_timedrdlock,
                   (pthread_rwlock_t * rwlock, const timespec *abstime)) {
  return mmix::rwlock_clocklock<rwlock::Role::Reader>(rwlock, CLOCK_REALTIME,
                                                      abstime);
}
} // namespace LIBC_NAMESPACE_DECL
