//===-- Linux implementation of renameat ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/renameat.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, renameat,
                   (int oldfd, const char *oldpath, int newfd,
                    const char *newpath)) {
#ifdef SYS_renameat
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_renameat, oldfd, oldpath,
                                              newfd, newpath);
#elif defined(SYS_renameat2)
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_renameat2, oldfd, oldpath,
                                              newfd, newpath, 0);
#else
#error "renameat and renameat2 syscalls not available."
#endif
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
