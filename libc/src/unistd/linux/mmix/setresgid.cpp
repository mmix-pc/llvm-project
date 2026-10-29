//===-- MMIX Linux implementation of setresgid ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/setresgid.h"

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/credentials.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setresgid, (gid_t rgid, gid_t egid, gid_t sgid)) {
  long result = mmix::change_credentials(SYS_setresgid, rgid, egid, sgid);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
