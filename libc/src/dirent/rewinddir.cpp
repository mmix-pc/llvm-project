//===-- Implementation of rewinddir ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/rewinddir.h"
#include "src/__support/File/dir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, rewinddir, (DIR * dir)) {
  int error = reinterpret_cast<Dir *>(dir)->seek(0);
  if (error)
    libc_errno = error;
}
} // namespace LIBC_NAMESPACE_DECL
