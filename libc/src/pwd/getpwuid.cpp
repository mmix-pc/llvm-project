//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/getpwuid.h"
#include "src/__support/common.h"
#include "src/pwd/getpwuid_r.h"
#include "src/pwd/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct passwd *, getpwuid, (uid_t uid)) {
  return pwd::lookup_buffer.lookup([uid](struct passwd *entry, char *buffer,
                                         size_t size, struct passwd **result) {
    return getpwuid_r(uid, entry, buffer, size, result);
  });
}
} // namespace LIBC_NAMESPACE_DECL
