//===-- MMIX Linux credential admission -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/seteuid.h"
#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/credentials.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, seteuid, (uid_t id)) {
  if (id == static_cast<uid_t>(-1)) {
    libc_errno = EINVAL;
    return -1;
  }
  long result = mmix::change_credentials(
      SYS_setresuid, static_cast<uid_t>(-1), id, static_cast<uid_t>(-1));
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
