//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/shadow/getspnam_r.h"
#include "src/__support/common.h"
#include "src/shadow/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getspnam_r,
                   (const char *name, struct spwd *entry, char *buffer,
                    size_t size, struct spwd **result)) {
  if (!name) {
    if (result)
      *result = nullptr;
    return EINVAL;
  }
  return shadow_db::lookup_shadow(
      [name](const struct spwd &candidate) {
        return cpp::string_view(candidate.sp_namp) == name;
      },
      entry, buffer, size, result);
}
} // namespace LIBC_NAMESPACE_DECL
