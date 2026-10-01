//===-- MMIX Linux credential admission -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/setgid.h"
#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/process_operation.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setgid, (gid_t id)) {
  mmix::ProcessOperation operation;
  if (operation.error()) {
    libc_errno = operation.error();
    return -1;
  }
  auto result = linux_syscalls::syscall_checked<int>(SYS_setgid, id);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
