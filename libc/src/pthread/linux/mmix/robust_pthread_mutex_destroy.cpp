//===-- MMIX Linux pthread_mutex_destroy -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "robust_mutex_dispatch.h"
#include "src/__support/common.h"
#include "src/pthread/pthread_mutex_destroy.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutex_destroy, (pthread_mutex_t * mutex)) {
  if (mutex->__robust)
    return reinterpret_cast<mmix::RobustMutex *>(mutex)->destroy();
  Mutex::destroy(reinterpret_cast<Mutex *>(mutex));
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
