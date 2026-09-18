//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/mntent/setmntent.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(::FILE *, setmntent, (const char *path, const char *mode)) {
  if (!path || !mode) {
    libc_errno = EINVAL;
    return nullptr;
  }
  auto opened = openfile(path, mode);
  if (!opened) {
    libc_errno = opened.error();
    return nullptr;
  }
  return reinterpret_cast<::FILE *>(opened.value());
}
} // namespace LIBC_NAMESPACE_DECL
