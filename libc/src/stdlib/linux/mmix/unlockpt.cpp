//===-- MMIX Linux pseudo-terminal unlocking ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/unlockpt.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_ioctl_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, unlockpt, (int fd)) {
  int unlocked = 0;
  auto result = linux_syscalls::ioctl(fd, TIOCSPTLCK, &unlocked);
  if (!result) {
    libc_errno = result.error() == ENOTTY ? EINVAL : result.error();
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
