//===-- Linux implementation of msgrcv ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/msg/msgrcv.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, msgrcv,
                   (int msqid, void *msgp, size_t msgsz, long msgtyp,
                    int msgflg)) {
  ssize_t ret = LIBC_NAMESPACE::syscall_impl<ssize_t>(SYS_msgrcv, msqid, msgp,
                                                      msgsz, msgtyp, msgflg);
  if (ret < 0) {
    libc_errno = static_cast<int>(-ret);
    return -1;
  }
  // msgsz and the return value exclude the leading message type.
  LIBC_MSAN_UNPOISON(static_cast<char *>(msgp), sizeof(long) + ret);
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
