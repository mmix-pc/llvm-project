//===-- Implementation of telldir -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/telldir.h"
#include "src/__support/File/dir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(long, telldir, (DIR * dir)) {
  auto result = reinterpret_cast<Dir *>(dir)->tell();
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}
} // namespace LIBC_NAMESPACE_DECL
