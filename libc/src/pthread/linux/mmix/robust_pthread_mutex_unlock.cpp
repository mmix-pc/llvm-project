//===-- MMIX Linux pthread_mutex_unlock -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "robust_mutex_dispatch.h"
#include "src/__support/common.h"
#include "src/pthread/pthread_mutex_unlock.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutex_unlock, (pthread_mutex_t * mutex)) {
  return mmix::mutex_result(
      (mutex->__robust || mutex->__priority_inherit)
          ? reinterpret_cast<mmix::RobustMutex *>(mutex)->unlock()
          : reinterpret_cast<Mutex *>(mutex)->unlock());
}
} // namespace LIBC_NAMESPACE_DECL
