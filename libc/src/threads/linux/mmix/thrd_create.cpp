//===-- MMIX Linux C thread creation --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/thrd_create.h"
#include "config/app.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/stack_policy.h"
#include "src/__support/threads/thread.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, thrd_create,
                   (thrd_t *output, thrd_start_t runner, void *argument)) {
  Thread thread;
  int error = thread.run(runner, argument, nullptr, mmix::DEFAULT_STACK_SIZE,
                         app.page_size);
  if (error)
    return error == ENOMEM ? thrd_nomem : thrd_error;
  static_assert(sizeof(thrd_t) == sizeof(Thread));
  reinterpret_cast<Thread *>(output)->attrib = thread.attrib;
  return thrd_success;
}

} // namespace LIBC_NAMESPACE_DECL
