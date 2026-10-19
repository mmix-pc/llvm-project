//===-- MMIX Linux supplementary group admission ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/initgroups.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/setgroups.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/credentials.h"
#include "src/grp/supplementary_groups.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, initgroups, (const char *user, gid_t group)) {
  internal::SupplementaryGroups groups;
  if (int error = groups.collect(user, group)) {
    libc_errno = error;
    return -1;
  }
  // The array must remain alive until every kernel task acknowledges it.
  long result = mmix::change_credentials(
      SYS_setgroups, groups.size, reinterpret_cast<long>(groups.data));
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
