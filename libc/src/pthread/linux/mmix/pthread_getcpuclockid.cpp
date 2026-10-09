//===-- MMIX Linux pthread_getcpuclockid ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_getcpuclockid.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_clock.h"
#include "src/__support/threads/thread.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_getcpuclockid,
                   (pthread_t handle, clockid_t *clock)) {
  return mmix::thread_clock(reinterpret_cast<Thread *>(&handle)->attrib,
                            *clock);
}
} // namespace LIBC_NAMESPACE_DECL
