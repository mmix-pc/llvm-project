//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/mntent/endmntent.h"
#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, endmntent, (::FILE * stream)) {
  if (stream) {
    int error = reinterpret_cast<File *>(stream)->close();
    if (error)
      libc_errno = error;
  }
  return 1;
}
} // namespace LIBC_NAMESPACE_DECL
