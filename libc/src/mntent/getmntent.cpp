//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/mntent/getmntent.h"
#include "hdr/func/realloc.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/mntent/mntent_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
// Nonreentrant, process-owned storage; the next getmntent call may overwrite
// it.
struct mntent result;
char *buffer = nullptr;
size_t capacity = 0;
} // namespace
LLVM_LIBC_FUNCTION(struct mntent *, getmntent, (::FILE * stream)) {
  if (!stream) {
    libc_errno = EINVAL;
    return nullptr;
  }
  return internal::read_mount_entry(
      reinterpret_cast<File *>(stream), &result, buffer, capacity,
      [](void *ptr, size_t size) { return ::realloc(ptr, size); });
}
} // namespace LIBC_NAMESPACE_DECL
