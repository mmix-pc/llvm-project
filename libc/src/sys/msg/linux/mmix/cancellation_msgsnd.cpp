//===-- MMIX Linux cancellable msgsnd -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/msg/msgsnd.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, msgsnd,
                   (int msqid, const void *msgp, size_t msgsz, int msgflg)) {
  long result = mmix::cancellation_syscall(
      SYS_msgsnd, msqid, reinterpret_cast<long>(msgp), msgsz, msgflg);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}

} // namespace LIBC_NAMESPACE_DECL
