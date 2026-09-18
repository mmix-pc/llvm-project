//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/mntent/getmntent_r.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/mntent/mntent_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(struct mntent *, getmntent_r,
                   (::FILE * stream, struct mntent *entry, char *buffer,
                    int size)) {
  if (!stream || !entry || !buffer || size <= 0) {
    libc_errno = EINVAL;
    return nullptr;
  }
  size_t capacity = static_cast<size_t>(size);
  return internal::read_mount_entry(reinterpret_cast<File *>(stream), entry,
                                    buffer, capacity);
}
} // namespace LIBC_NAMESPACE_DECL
