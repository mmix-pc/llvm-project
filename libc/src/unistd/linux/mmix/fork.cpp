//===-- MMIX Linux same-stack fork ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/fork.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/fork_callbacks.h"
#include "src/__support/threads/identifier.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
#error "MMIX Linux fork currently requires single-thread mode"
#endif

namespace LIBC_NAMESPACE_DECL {

// FIXME: Rejoin shared fork policy when pthread/TLS and coordinated
// sigaction/abort/fork/spawn synchronization are supported on MMIX Linux.
LLVM_LIBC_FUNCTION(pid_t, fork, (void)) {
  auto *attributes = current_thread().attrib;
  if (!attributes) {
    libc_errno = EINVAL;
    return -1;
  }

  invoke_prepare_callbacks();
  sigset_t full{{-1UL}}, saved{};
  auto blocked = linux_syscalls::rt_sigprocmask(SIG_BLOCK, &full, &saved);
  if (!blocked) {
    invoke_parent_callbacks();
    libc_errno = blocked.error();
    return -1;
  }

  pid_t parent_tid = attributes->tid;
  attributes->tid = 0;
  // Linux copies both stacks and resumes after TRAP with r231=0 in the child.
  // The syscall leaf can POP normally; no foreign new-stack clone recipe is used.
  long result = syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
  if (result == 0) {
    long tid = syscall_impl<long>(SYS_gettid);
    if (tid <= 0 || tid > cpp::numeric_limits<pid_t>::max())
      internal::exit(127);
    attributes->tid = static_cast<pid_t>(tid);
  } else {
    attributes->tid = parent_tid;
  }

  // Publish valid identity before handlers or callbacks can observe the child.
  // A created child must never be disguised as a failed fork in the parent.
  if (!linux_syscalls::rt_sigprocmask(SIG_SETMASK, &saved, nullptr))
    internal::exit(127);
  if (result == 0) {
    invoke_child_callbacks();
  } else {
    invoke_parent_callbacks();
  }
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<pid_t>(result);
}

} // namespace LIBC_NAMESPACE_DECL
