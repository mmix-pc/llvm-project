//===-- MMIX Linux pseudo-terminal pathname storage -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/ptsname.h"
#include "hdr/limits_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/stdlib/ptsname_r.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, ptsname, (int fd)) {
  static char buffer[PATH_MAX];
  int error = LIBC_NAMESPACE::ptsname_r(fd, buffer, sizeof(buffer));
  if (error) {
    libc_errno = error;
    return nullptr;
  }
  return buffer;
}
} // namespace LIBC_NAMESPACE_DECL
