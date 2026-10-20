//===-- Implementation of getgrouplist
//-------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/getgrouplist.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/common.h"
#include "src/grp/supplementary_groups.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getgrouplist,
                   (const char *user, gid_t group, gid_t *groups, int *count)) {
  if (!count || *count < 0 || (*count && !groups)) {
    libc_errno = EINVAL;
    return -1;
  }
  internal::SupplementaryGroups collected;
  if (int error = collected.collect(user, group)) {
    libc_errno = error;
    return -1;
  }
  if (collected.size > static_cast<size_t>(cpp::numeric_limits<int>::max())) {
    libc_errno = EOVERFLOW;
    return -1;
  }
  int capacity = *count;
  *count = static_cast<int>(collected.size);
  for (int i = 0; i < capacity && i < *count; ++i)
    groups[i] = collected.data[i];
  return capacity < *count ? -1 : *count;
}
} // namespace LIBC_NAMESPACE_DECL
