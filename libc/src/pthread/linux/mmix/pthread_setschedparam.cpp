//===-- MMIX Linux pthread_setschedparam ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_setschedparam.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_scheduling.h"
#include "src/__support/threads/thread.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_setschedparam,
                   (pthread_t handle, int policy,
                    const struct sched_param *value)) {
  int priority = value->sched_priority;
  return mmix::schedule_thread(reinterpret_cast<Thread *>(&handle)->attrib,
                               policy, priority, true);
}
} // namespace LIBC_NAMESPACE_DECL
