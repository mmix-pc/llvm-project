//===-- Linux implementation of mq_unlink ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_unlink.h"

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_unlink, (const char *name)) {
  if (name[0] != '/') {
    libc_errno = EINVAL;
    return -1;
  }
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_mq_unlink, name + 1);
  if (ret < 0) {
    // The VFS can report EPERM, but mq_unlink reports permission denial as
    // EACCES, including sticky-directory ownership failures.
    libc_errno = ret == -EPERM ? EACCES : -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
