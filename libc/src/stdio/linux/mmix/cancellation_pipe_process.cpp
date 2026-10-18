//===-- MMIX cancellation-safe pipe subprocess ownership ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/sys_wait_macros.h"
#include "hdr/types/siginfo_t.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/wait4.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/stdio/linux/pipe_process.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
PipeProcess::PipeProcess() {
  if (mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &old_state) ||
      mmix::set_cancel_type(PTHREAD_CANCEL_DEFERRED, &old_type))
    __builtin_trap();
}
void PipeProcess::disable() {
  if (mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, nullptr))
    __builtin_trap();
}
void PipeProcess::resume() {
  if (mmix::set_cancel_state(old_state, nullptr))
    __builtin_trap();
}
void PipeProcess::restore() {
  if (restored)
    return;
  restored = true;
  if (mmix::set_cancel_type(old_type, nullptr) ||
      mmix::set_cancel_state(old_state, nullptr))
    __builtin_trap();
}
int PipeProcess::wait(int &status) {
  if (pid <= 0)
    return ECHILD;
  resume();
  // Retain the child PID across the cancellation window. Only reap after
  // disabling cancellation, so the ownership transition cannot unwind.
  siginfo_t info{};
  long result;
  do {
    result = mmix::cancellation_syscall(SYS_waitid, P_PID, pid,
                                        reinterpret_cast<long>(&info),
                                        WEXITED | WNOWAIT, 0);
  } while (result == -EINTR);
  disable();
  if (result < 0) {
    if (result == -ECHILD)
      pid = -1;
    return static_cast<int>(-result);
  }
  auto waited = linux_syscalls::wait4(pid, &status, 0, nullptr);
  while (!waited && waited.error() == EINTR)
    waited = linux_syscalls::wait4(pid, &status, 0, nullptr);
  if (!waited) {
    if (waited.error() == ECHILD)
      pid = -1;
    return waited.error();
  }
  pid = -1;
  return 0;
}
PipeProcess::~PipeProcess() noexcept(false) {
  if (restored && pid <= 0)
    return;
  int error = libc_errno;
  disable();
  if (pid > 0) {
    // Cleanup may wait for the shell, but must not signal a PID that SIGCHLD
    // auto-reaping or an application waiter could already have released.
    auto result = linux_syscalls::wait4(pid, nullptr, 0, nullptr);
    while (!result && result.error() == EINTR)
      result = linux_syscalls::wait4(pid, nullptr, 0, nullptr);
  }
  libc_errno = error;
  restore();
}
} // namespace LIBC_NAMESPACE_DECL
