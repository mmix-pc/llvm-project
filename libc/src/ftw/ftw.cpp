//===-- Implementation of ftw ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ftw/ftw.h"
#include "src/__support/common.h"
#include "src/ftw/walk.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ftw,
                   (const char *path,
                    int (*callback)(const char *, const struct stat *, int),
                    int fd_limit)) {
  return ftw_internal::walk(path, {nullptr, callback}, fd_limit, 0);
}

} // namespace LIBC_NAMESPACE_DECL
