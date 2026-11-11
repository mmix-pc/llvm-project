//===-- MMIX Linux pthread supplied-stack attributes ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_attr_setstack.h"

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/linux/mmix/stack_policy.h"

#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, pthread_attr_setstack,
                   (pthread_attr_t *__restrict attr, void *stack,
                    size_t stacksize)) {
  // The selected MMIX Linux TASK_SIZE is the exclusive user-address end.
  constexpr uintptr_t USER_ADDRESS_END = uintptr_t{1} << 43;
  constexpr uintptr_t STACK_ALIGNMENT = 8;
  const uintptr_t base = reinterpret_cast<uintptr_t>(stack);
  if (stacksize < PTHREAD_STACK_MIN || stacksize > mmix::MAX_STACK_SIZE ||
      base >= USER_ADDRESS_END || stacksize > USER_ADDRESS_END - base ||
      base % STACK_ALIGNMENT || (base + stacksize) % STACK_ALIGNMENT)
    return EINVAL;
  attr->__stack = stack;
  attr->__stacksize = stacksize;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
