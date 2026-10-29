//===-- Linux implementation of fexecve -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/fexecve.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, fexecve,
                   (int fd, char *const argv[], char *const envp[])) {
  if (fd < 0) {
    libc_errno = EBADF;
    return -1;
  }

  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_execveat, fd, "", argv, envp,
                                            AT_EMPTY_PATH);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }

  // Successful execution replaces the image and does not return.
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
