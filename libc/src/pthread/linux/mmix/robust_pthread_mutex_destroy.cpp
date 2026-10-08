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
#ifdef LIBC_MMIX_PROTECT_MUTEX
  if (mmix::uses_protect_mutex(mutex))
    return reinterpret_cast<mmix::ProtectMutex *>(mutex)->destroy();
#endif
  if (mmix::uses_kernel_mutex(mutex))
    return reinterpret_cast<mmix::RobustMutex *>(mutex)->destroy();
  Mutex::destroy(reinterpret_cast<Mutex *>(mutex));
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
