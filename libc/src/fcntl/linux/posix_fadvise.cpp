//===-- Linux implementation of posix_fadvise -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/fcntl/posix_fadvise.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// Select only for a reviewed native LP64 syscall ABI (MMIX and x86-64).
// A 32-bit fadvise64 may have a narrow length or split/reordered arguments.
static_assert(sizeof(long) == 8 && sizeof(void *) == 8 && sizeof(off_t) == 8 &&
                  off_t(-1) < off_t(0),
              "posix_fadvise requires a native LP64 syscall ABI");

LLVM_LIBC_FUNCTION(int, posix_fadvise,
                   (int fd, off_t offset, off_t len, int advice)) {
  // Linux's 64-bit syscall table names this fadvise64 even when the handler
  // is sys_fadvise64_64. Leave range/advice validation (including len == 0)
  // to the kernel; unlike allocation, advice need not fit within the file.
  long ret = LIBC_NAMESPACE::syscall_impl<long>(SYS_fadvise64, fd, offset, len,
                                                advice);
  return ret < 0 ? static_cast<int>(-ret) : 0;
}

} // namespace LIBC_NAMESPACE_DECL
