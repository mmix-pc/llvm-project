//===-- MMIX Linux cancelable pthread join --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/cancellable_join.h"
#include "src/__support/threads/thread.h"
#include "src/pthread/pthread_join.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_join, (pthread_t th, void **retval)) {
  static_assert(sizeof(pthread_t) == sizeof(Thread));
  auto *thread = reinterpret_cast<Thread *>(&th);
  ThreadReturnValue result;
  int error = mmix::cancellable_join(thread->attrib, result);
  if (error)
    return error;
  if (retval)
    *retval = result.posix_retval;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
