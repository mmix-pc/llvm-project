//===-- MMIX Linux cancelable condition wait ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cancellation_condition.h"
#include "src/pthread/pthread_cond_wait.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_cond_wait,
                   (pthread_cond_t *__restrict cond,
                    pthread_mutex_t *__restrict mutex)) {
  return mmix::condition_wait(pthread_cond_utils::to_cndvar(cond),
                              pthread_cond_utils::to_mutex(mutex),
                              cpp::nullopt);
}
} // namespace LIBC_NAMESPACE_DECL
