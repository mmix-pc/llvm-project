//===-- MMIX Linux cancellable waitpid -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/wait/waitpid.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(pid_t, waitpid, (pid_t pid, int *wait_status, int options)) {
  long result = mmix::cancellation_process_wait(
      SYS_wait4, pid, reinterpret_cast<long>(wait_status), options, 0);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<pid_t>(result);
}

} // namespace LIBC_NAMESPACE_DECL
