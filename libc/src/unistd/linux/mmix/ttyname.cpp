//===-- MMIX Linux terminal pathname storage ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/unistd/ttyname.h"
#include "hdr/limits_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/unistd/ttyname_r.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, ttyname, (int fd)) {
  static char buffer[PATH_MAX];
  int error = LIBC_NAMESPACE::ttyname_r(fd, buffer, sizeof(buffer));
  if (error) {
    libc_errno = error;
    return nullptr;
  }
  return buffer;
}
} // namespace LIBC_NAMESPACE_DECL
