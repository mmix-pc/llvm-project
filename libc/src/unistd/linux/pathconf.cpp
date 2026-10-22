//===-- Linux implementation of pathconf ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/pathconf.h"
#include "hdr/types/struct_statfs.h"
#include "hdr/unistd_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statfs.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/unistd/linux/pathconf_utils.h"
#include "src/unistd/linux/fallocate_query.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(long, pathconf, (const char *path, int name)) {
#ifdef _PC_FALLOC
  if (name == _PC_FALLOC) {
    auto result = fallocate_support(path);
    if (!result) {
      libc_errno = result.error();
      return -1;
    }
    return *result;
  }
#endif
  struct statfs result;
  auto error_or_ret = linux_syscalls::statfs(path, &result);
  if (!error_or_ret) {
    libc_errno = error_or_ret.error();
    return -1;
  }
  return pathconfig(result, name);
}

} // namespace LIBC_NAMESPACE_DECL
