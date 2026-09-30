//===-- MMIX Linux implementation of pthread_detach -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_detach.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_detach.h"
#include <pthread.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// Thread::detach returns a cleanup classification, not errors. Call the MMIX
// provider directly to preserve its ownership-check error codes.
LLVM_LIBC_FUNCTION(int, pthread_detach, (pthread_t th)) {
  uint64_t blocked = UINT64_MAX, saved = 0;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&blocked),
                   reinterpret_cast<long>(&saved), sizeof(saved)) != 0)
    __builtin_trap();
  int error = mmix::detach_thread(static_cast<ThreadAttributes *>(th.__attrib));
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&saved), 0, sizeof(saved)) != 0)
    __builtin_trap();
  return error;
}

} // namespace LIBC_NAMESPACE_DECL
