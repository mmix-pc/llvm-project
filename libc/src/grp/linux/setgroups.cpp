//===-- Implementation of setgroups ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/setgroups.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/setgroups.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setgroups, (size_t count, const gid_t *groups)) {
  auto result = linux_syscalls::setgroups(count, groups);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
