//===-- Linux implementation of nice -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/nice.h"

#include "hdr/errno_macros.h"
#include "hdr/sys_resource_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getpriority.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/setpriority.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, nice, (int increment)) {
  // Linux PRIO_PROCESS/0 selects the calling task, not every process thread.
  // The raw result distinguishes a legitimate nice value of -1 from an error
  // without clearing the caller's errno.
  auto old_priority = linux_syscalls::getpriority(PRIO_PROCESS, 0);
  if (!old_priority) {
    libc_errno = old_priority.error();
    return -1;
  }

  const int current = 20 - old_priority.value();
  // Compare before adding: even INT_MIN/INT_MAX increments must not overflow.
  int requested;
  if (increment > 19 - current)
    requested = 19;
  else if (increment < -20 - current)
    requested = -20;
  else
    requested = current + increment;

  auto changed = linux_syscalls::setpriority(PRIO_PROCESS, 0, requested);
  if (!changed) {
    libc_errno = changed.error() == EACCES ? EPERM : changed.error();
    return -1;
  }

  // Report the observed new value, not the syscall's zero success result.
  auto new_priority = linux_syscalls::getpriority(PRIO_PROCESS, 0);
  if (!new_priority) {
    libc_errno = new_priority.error();
    return -1;
  }
  return 20 - new_priority.value();
}

} // namespace LIBC_NAMESPACE_DECL
