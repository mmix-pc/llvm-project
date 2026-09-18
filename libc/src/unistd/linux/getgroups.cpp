//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/unistd/getgroups.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getgroups, (int size, gid_t list[])) {
  if (size < 0) {
    libc_errno = EINVAL;
    return -1;
  }
#if defined(SYS_getgroups32)
  constexpr long number = SYS_getgroups32;
#else
  constexpr long number = SYS_getgroups;
#endif
  auto result = linux_syscalls::syscall_checked<int>(number, size, list);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}
} // namespace LIBC_NAMESPACE_DECL
