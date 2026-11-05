//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/thrd_join.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/cancellable_join.h"
#include "src/__support/threads/thread.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, thrd_join, (thrd_t thread, int *value)) {
  static_assert(sizeof(thrd_t) == sizeof(Thread));
  auto *internal = reinterpret_cast<Thread *>(&thread);
  ThreadReturnValue result;
  if (mmix::cancellable_join(internal->attrib, result))
    return thrd_error;
  if (value)
    *value = result.stdc_retval;
  return thrd_success;
}

} // namespace LIBC_NAMESPACE_DECL
