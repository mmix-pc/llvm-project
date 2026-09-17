//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/getpwuid_r.h"
#include "src/__support/common.h"
#include "src/pwd/linux/mmix/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getpwuid_r,
                   (uid_t uid, struct passwd *entry, char *buffer, size_t size,
                    struct passwd **result)) {
  return pwd::lookup_passwd(
      [uid](const struct passwd &candidate) { return candidate.pw_uid == uid; },
      entry, buffer, size, result);
}
} // namespace LIBC_NAMESPACE_DECL
