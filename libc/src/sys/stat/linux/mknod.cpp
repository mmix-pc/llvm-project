//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/stat/mknod.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mknod, (const char *path, mode_t mode, dev_t dev)) {
  // Linux transports device numbers in a 32-bit syscall argument.
  if (static_cast<unsigned int>(dev) != dev) {
    libc_errno = EINVAL;
    return -1;
  }
#if defined(SYS_mknodat)
  int ret = syscall_impl<int>(SYS_mknodat, AT_FDCWD, path, mode,
                              static_cast<unsigned int>(dev));
#else
  int ret =
      syscall_impl<int>(SYS_mknod, path, mode, static_cast<unsigned int>(dev));
#endif
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
