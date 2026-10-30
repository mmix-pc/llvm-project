//===-- Linux implementation of shmat -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/shm/shmat.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void *, shmat,
                   (int shmid, const void *shmaddr, int shmflg)) {
  // Only [-4095, -1] denotes an error, not every address with its high bit set.
  auto result = linux_syscalls::syscall_checked<void *>(SYS_shmat, shmid,
                                                        shmaddr, shmflg);
  if (!result) {
    libc_errno = result.error();
    return reinterpret_cast<void *>(-1);
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
