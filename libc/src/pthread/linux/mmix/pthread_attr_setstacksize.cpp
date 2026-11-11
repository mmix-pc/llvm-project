//===-- MMIX Linux pthread stack-size attributes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_attr_setstacksize.h"

#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/linux/mmix/stack_policy.h"

#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, pthread_attr_setstacksize,
                   (pthread_attr_t *__restrict attr, size_t stacksize)) {
  if (stacksize < PTHREAD_STACK_MIN || stacksize > mmix::MAX_STACK_SIZE)
    return EINVAL;
  // Preserve any application-supplied address when changing only the capacity.
  attr->__stacksize = stacksize;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
