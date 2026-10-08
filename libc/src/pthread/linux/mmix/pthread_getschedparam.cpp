//===-- MMIX Linux pthread_getschedparam ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_getschedparam.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_scheduling.h"
#include "src/__support/threads/thread.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_getschedparam,
                   (pthread_t handle, int *__restrict policy,
                    struct sched_param *__restrict value)) {
  int p = 0, priority = 0;
  if (int error = mmix::schedule_thread(
          reinterpret_cast<Thread *>(&handle)->attrib, p, priority))
    return error;
  *policy = p;
  value->sched_priority = priority;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
