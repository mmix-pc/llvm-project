//===-- Implementation of nftw --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/ftw/nftw.h"
#include "src/__support/common.h"
#include "src/ftw/walk.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, nftw,
                   (const char *path,
                    int (*callback)(const char *, const struct stat *, int,
                                    struct FTW *),
                    int fd_limit, int flags)) {
  return ftw_internal::walk(path, {callback, nullptr}, fd_limit, flags);
}

} // namespace LIBC_NAMESPACE_DECL
