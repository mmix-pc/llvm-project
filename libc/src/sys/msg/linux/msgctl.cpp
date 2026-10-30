//===-- Linux implementation of msgctl ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/msg/msgctl.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// The direct syscall uses the native IPC_64 layout. Legacy 32-bit IPC needs
// separate command-version and time-layout handling before enabling this path.
static_assert(sizeof(long) == 8 && sizeof(void *) == 8,
              "msgctl requires the native 64-bit IPC ABI");

LLVM_LIBC_FUNCTION(int, msgctl, (int msqid, int cmd, struct msqid_ds *buf)) {
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_msgctl, msqid, cmd, buf);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
