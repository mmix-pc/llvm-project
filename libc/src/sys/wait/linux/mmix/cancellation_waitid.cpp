//===-- MMIX Linux cancellable waitid -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/wait/waitid.h"

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, waitid,
                   (idtype_t type, id_t id, siginfo_t *info, int options)) {
  long result = mmix::cancellation_process_wait(
      SYS_waitid, type, id, reinterpret_cast<long>(info), options, 0);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}

} // namespace LIBC_NAMESPACE_DECL
