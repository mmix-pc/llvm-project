//===-- MMIX Linux pthread_setschedprio -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_setschedprio.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_scheduling.h"
#include "src/__support/threads/thread.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_setschedprio,
                   (pthread_t handle, int priority)) {
  int policy = 0;
  return mmix::schedule_thread(reinterpret_cast<Thread *>(&handle)->attrib,
                               policy, priority, true, true);
}
} // namespace LIBC_NAMESPACE_DECL
