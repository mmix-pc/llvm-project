//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/unistd/setegid.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setegid, (gid_t gid)) {
  if (gid == static_cast<gid_t>(-1)) {
    libc_errno = EINVAL;
    return -1;
  }
#if defined(SYS_setresgid32)
  constexpr long number = SYS_setresgid32;
#else
  constexpr long number = SYS_setresgid;
#endif
  auto result = linux_syscalls::syscall_checked<int>(
      number, static_cast<gid_t>(-1), gid, static_cast<gid_t>(-1));
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
