//===-- MMIX Linux sigqueue implementation --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigqueue.h"

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "private_signals.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(siginfo_t) == 128);

LLVM_LIBC_FUNCTION(int, sigqueue,
                   (pid_t pid, int sig, const union sigval value)) {
  if (mmix::private_signal(sig)) {
    libc_errno = EINVAL;
    return -1;
  }

  // Include ABI padding and unused union storage in the initialization.
  siginfo_t info;
  __builtin_memset(&info, 0, sizeof(info));
  info.si_signo = sig;
  info.si_code = SI_QUEUE;
  info.si_value = value;

  long sender = syscall_impl<long>(SYS_getpid);
  if (sender < 0) {
    libc_errno = static_cast<int>(-sender);
    return -1;
  }
  long uid = syscall_impl<long>(SYS_getuid);
  if (uid < 0) {
    libc_errno = static_cast<int>(-uid);
    return -1;
  }
  info.si_pid = static_cast<pid_t>(sender);
  info.si_uid = static_cast<uid_t>(uid);

  // Signal zero uses the same PID/permission checks without queuing a signal.
  long result = syscall_impl<long>(SYS_rt_sigqueueinfo, pid, sig, &info);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
