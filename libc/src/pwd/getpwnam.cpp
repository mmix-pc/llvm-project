//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/pwd/getpwnam.h"
#include "src/__support/common.h"
#include "src/pwd/getpwnam_r.h"
#include "src/pwd/lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct passwd *, getpwnam, (const char *name)) {
  return pwd::lookup_buffer.lookup([name](struct passwd *entry, char *buffer,
                                          size_t size, struct passwd **result) {
    return getpwnam_r(name, entry, buffer, size, result);
  });
}
} // namespace LIBC_NAMESPACE_DECL
