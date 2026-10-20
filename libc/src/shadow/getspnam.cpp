//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/shadow/getspnam.h"
#include "src/__support/common.h"
#include "src/shadow/getspnam_r.h"
#include "src/shadow/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct spwd *, getspnam, (const char *name)) {
  return shadow_db::lookup_buffer.lookup([name](struct spwd *entry,
                                                char *buffer, size_t size,
                                                struct spwd **result) {
    return LIBC_NAMESPACE::getspnam_r(name, entry, buffer, size, result);
  });
}
} // namespace LIBC_NAMESPACE_DECL
