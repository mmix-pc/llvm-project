//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/stat/mkfifo.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/common.h"
#include "src/sys/stat/mknod.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, mkfifo, (const char *path, mode_t mode)) {
  return LIBC_NAMESPACE::mknod(path, (mode & ~S_IFMT) | S_IFIFO, 0);
}
} // namespace LIBC_NAMESPACE_DECL
