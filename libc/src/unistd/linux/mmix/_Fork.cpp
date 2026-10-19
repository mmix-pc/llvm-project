//===-- MMIX Linux async-signal-safe _Fork --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/_Fork.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/main_thread.h"
#include <stdint.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(pid_t, _Fork, (void)) {
  int saved_errno = libc_errno;
  uint64_t full = UINT64_MAX, saved = 0;
  long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &full, &saved,
                                   sizeof(saved));
  if (result < 0) {
    libc_errno = int(-result);
    return -1;
  }
  // The kernel copies both stacks and returns normally through the syscall
  // leaf. Do not enter coordinated fork, callbacks, allocation or loader locks.
  result = syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
  if (!result && !internal::restore_minimal_fork_child())
    internal::exit(127);
  // Publish the child identity before inherited handlers can run. A child
  // already created must never be reported as a failed clone to its parent.
  if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved, 0,
                         sizeof(saved)))
    internal::exit(127);
  libc_errno = result < 0 ? int(-result) : saved_errno;
  return result < 0 ? -1 : pid_t(result);
}
} // namespace LIBC_NAMESPACE_DECL
