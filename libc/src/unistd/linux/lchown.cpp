//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/lchown.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, lchown, (const char *path, uid_t owner, gid_t group)) {
#if defined(SYS_lchown)
  int ret = syscall_impl<int>(SYS_lchown, path, owner, group);
#else
  int ret = syscall_impl<int>(SYS_fchownat, AT_FDCWD, path, owner, group,
                              AT_SYMLINK_NOFOLLOW);
#endif
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
