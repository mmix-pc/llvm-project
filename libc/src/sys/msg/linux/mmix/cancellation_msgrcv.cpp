//===-- MMIX Linux cancellable msgrcv -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/msg/msgrcv.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, msgrcv,
                   (int msqid, void *msgp, size_t msgsz, long msgtyp,
                    int msgflg)) {
  long result = mmix::cancellation_syscall(
      SYS_msgrcv, msqid, reinterpret_cast<long>(msgp), msgsz, msgtyp, msgflg);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  LIBC_MSAN_UNPOISON(static_cast<char *>(msgp), sizeof(long) + result);
  return static_cast<ssize_t>(result);
}

} // namespace LIBC_NAMESPACE_DECL
