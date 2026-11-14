//===-- MMIX Linux implementation of semop -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/sem/semop.h"

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, semop, (int semid, struct sembuf *sops, size_t nsops)) {
  // The Linux syscall takes an unsigned int count, whereas POSIX uses size_t.
  // Do not let narrowing execute a prefix of an oversized operation vector.
  // The smaller, namespace-specific SEMOPM limit remains the kernel's check.
  if (nsops > UINT_MAX) {
    libc_errno = E2BIG;
    return -1;
  }
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_semop, semid, sops, nsops);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
