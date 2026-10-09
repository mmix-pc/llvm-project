//===-- MMIX Linux coordinated same-stack fork ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/atfork_callbacks.h"
#include "src/__support/threads/linux/mmix/fork_runtime.h"
#include "src/unistd/fork.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(pid_t, fork, (void)) {
  int saved_errno = libc_errno;
  mmix::ForkOperation operation;
  if (operation.error()) {
    libc_errno = operation.error();
    return -1;
  }
  // Freeze callback registration only after internal locks have been prepared.
  size_t count = mmix::prepare_fork_callbacks();
  operation.prepare_runtime();
  mmix::lock_fork_callbacks();
  int error = operation.freeze_registry();
  long result =
      error ? -error : syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
  operation.restore_runtime(result == 0);
  mmix::finish_fork_callbacks(count, result == 0);
  if (result < 0) {
    libc_errno = int(-result);
    return -1;
  }
  libc_errno = saved_errno;
  return pid_t(result);
}
} // namespace LIBC_NAMESPACE_DECL
