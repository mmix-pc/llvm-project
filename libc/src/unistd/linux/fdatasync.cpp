//===-- Linux implementation of fdatasync ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/fdatasync.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// This is the raw syscall provider. Targets supporting thread cancellation
// must select a cancellable provider for this POSIX cancellation point.
LLVM_LIBC_FUNCTION(int, fdatasync, (int fd)) {
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_fdatasync, fd);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
