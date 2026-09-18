//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/grp/getgrnam_r.h"
#include "src/__support/common.h"
#include "src/grp/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getgrnam_r,
                   (const char *name, struct group *entry, char *buffer,
                    size_t size, struct group **result)) {
  if (!name) {
    if (result)
      *result = nullptr;
    return EINVAL;
  }
  return group_db::lookup_group(
      [name](const struct group &candidate) {
        return cpp::string_view(candidate.gr_name) == name;
      },
      entry, buffer, size, result);
}
} // namespace LIBC_NAMESPACE_DECL
