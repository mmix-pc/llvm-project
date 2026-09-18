//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/grp/getgrgid.h"
#include "src/__support/common.h"
#include "src/grp/getgrgid_r.h"
#include "src/grp/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct group *, getgrgid, (gid_t gid)) {
  return group_db::lookup_buffer.lookup([gid](struct group *entry, char *buffer,
                                              size_t size,
                                              struct group **result) {
    return getgrgid_r(gid, entry, buffer, size, result);
  });
}
} // namespace LIBC_NAMESPACE_DECL
