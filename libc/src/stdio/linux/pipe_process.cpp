//===-- Linux pipe subprocess ownership -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/stdio/linux/pipe_process.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/wait4.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
PipeProcess::PipeProcess() = default;
void PipeProcess::disable() {}
void PipeProcess::resume() {}
void PipeProcess::restore() {}
int PipeProcess::wait(int &status) {
  if (pid <= 0)
    return ECHILD;
  auto result = linux_syscalls::wait4(pid, &status, 0, nullptr);
  while (!result && result.error() == EINTR)
    result = linux_syscalls::wait4(pid, &status, 0, nullptr);
  if (!result) {
    if (result.error() == ECHILD)
      pid = -1;
    return result.error();
  }
  pid = -1;
  return 0;
}
PipeProcess::~PipeProcess() noexcept(false) {
  int error = libc_errno;
  if (pid > 0) {
    // Do not signal a numeric PID during cleanup: SIGCHLD auto-reaping or an
    // application waiter may already have released it for reuse.
    int status;
    (void)wait(status);
  }
  libc_errno = error;
}
} // namespace LIBC_NAMESPACE_DECL
