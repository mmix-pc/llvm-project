//===-- MMIX Linux pthread_mutex_lock -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "robust_mutex_dispatch.h"
#include "src/__support/common.h"
#include "src/pthread/pthread_mutex_lock.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutex_lock, (pthread_mutex_t * mutex)) {
  return mmix::mutex_result(
      mutex->__robust ? reinterpret_cast<mmix::RobustMutex *>(mutex)->lock()
                      : reinterpret_cast<Mutex *>(mutex)->lock());
}
} // namespace LIBC_NAMESPACE_DECL
