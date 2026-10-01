//===-- MMIX Linux credential admission -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/setegid.h"
#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/process_operation.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setegid, (gid_t id)) {
  mmix::ProcessOperation operation;
  if (operation.error()) {
    libc_errno = operation.error();
    return -1;
  }
  if (id == static_cast<gid_t>(-1)) {
    libc_errno = EINVAL;
    return -1;
  }
  auto result = linux_syscalls::syscall_checked<int>(
      SYS_setresgid, static_cast<gid_t>(-1), id, static_cast<gid_t>(-1));
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
