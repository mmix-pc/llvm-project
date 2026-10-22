//===---------- Linux implementation of the POSIX posix_madvise function --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/mman/posix_madvise.h"

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/syscall.h" // For internal syscall function.
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include <sys/syscall.h> // For syscall numbers.

#include "hdr/sys_mman_macros.h" // For POSIX_MADV_DONTNEED.

namespace LIBC_NAMESPACE_DECL {

// This function is currently linux only. It has to be refactored suitably if
// posix_madvise is to be supported on non-linux operating systems also.
LLVM_LIBC_FUNCTION(int, posix_madvise, (void *addr, size_t size, int advice)) {
  switch (advice) {
  case POSIX_MADV_NORMAL:
  case POSIX_MADV_RANDOM:
  case POSIX_MADV_SEQUENTIAL:
  case POSIX_MADV_WILLNEED:
    break;
  case POSIX_MADV_DONTNEED:
    // Linux MADV_DONTNEED can discard data; POSIX advice must preserve it.
    return 0;
  default:
    // Do not forward Linux-only advice with destructive semantics.
    return EINVAL;
  }
  int ret = LIBC_NAMESPACE::syscall_impl<int>(
      SYS_madvise, reinterpret_cast<long>(addr), size, advice);
  return ret < 0 ? -ret : 0;
}

} // namespace LIBC_NAMESPACE_DECL
