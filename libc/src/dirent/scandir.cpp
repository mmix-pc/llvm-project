//===-- Implementation of scandir -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/scandir.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/common.h"
#include "src/dirent/scandir_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, scandir,
                   (const char *path, struct dirent ***list,
                    int (*filter)(const struct dirent *),
                    int (*compare)(const struct dirent **,
                                   const struct dirent **))) {
  return internal::scan_directory(AT_FDCWD, path, list, filter, compare);
}
} // namespace LIBC_NAMESPACE_DECL
