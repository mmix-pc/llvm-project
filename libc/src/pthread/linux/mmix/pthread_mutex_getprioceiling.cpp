//===-- MMIX Linux priority ceiling interface -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_mutex_getprioceiling.h"
#include "hdr/errno_macros.h"
#include "mutex_protocol.h"
#include "src/__support/threads/linux/mmix/priority_protection_state.h"
#include "src/__support/threads/linux/mmix/protect_mutex.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutex_getprioceiling,
                   (const pthread_mutex_t *__restrict mutex,
                    int *__restrict value)) {
  if (!mmix::uses_protect_mutex(mutex))
    return EINVAL;
  *value = reinterpret_cast<const mmix::ProtectMutex *>(mutex)->ceiling();
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
