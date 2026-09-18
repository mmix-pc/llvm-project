//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/getpwnam_r.h"
#include "src/__support/common.h"
#include "src/pwd/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getpwnam_r,
                   (const char *name, struct passwd *entry, char *buffer,
                    size_t size, struct passwd **result)) {
  if (!name) {
    if (result)
      *result = nullptr;
    return EINVAL;
  }
  return pwd::lookup_passwd(
      [name](const struct passwd &candidate) {
        return cpp::string_view(candidate.pw_name) == name;
      },
      entry, buffer, size, result);
}
} // namespace LIBC_NAMESPACE_DECL
