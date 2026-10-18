//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/stat/mknod.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mknodat.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mknod, (const char *path, mode_t mode, dev_t dev)) {
#if defined(SYS_mknodat)
  auto result = linux_syscalls::mknodat(AT_FDCWD, path, mode, dev);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
#else
  auto encoded = linux_syscalls::encode_device(dev);
  if (!encoded) {
    libc_errno = encoded.error();
    return -1;
  }
  int ret = syscall_impl<int>(SYS_mknod, path, mode, *encoded);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
#endif
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
