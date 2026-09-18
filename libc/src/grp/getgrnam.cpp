//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/grp/getgrnam.h"
#include "src/__support/common.h"
#include "src/grp/getgrnam_r.h"
#include "src/grp/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct group *, getgrnam, (const char *name)) {
  return group_db::lookup_buffer.lookup([name](struct group *entry,
                                               char *buffer, size_t size,
                                               struct group **result) {
    return getgrnam_r(name, entry, buffer, size, result);
  });
}
} // namespace LIBC_NAMESPACE_DECL
