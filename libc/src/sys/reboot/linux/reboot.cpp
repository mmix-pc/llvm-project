//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/sys/reboot/reboot.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <linux/reboot.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, reboot, (int command)) {
  auto result = linux_syscalls::syscall_checked<int>(
      SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, command, nullptr);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}
} // namespace LIBC_NAMESPACE_DECL
