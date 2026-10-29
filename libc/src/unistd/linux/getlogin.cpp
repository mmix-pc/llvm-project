//===-- Linux borrowed login name storage ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/getlogin.h"

#include "hdr/utmp_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/unistd/getlogin_r.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, getlogin, ()) {
  // The non-reentrant API may invalidate its result on the next call.
  static char name[UT_NAMESIZE + 1];
  int error = LIBC_NAMESPACE::getlogin_r(name, sizeof(name));
  if (error) {
    libc_errno = error;
    return nullptr;
  }
  return name;
}
} // namespace LIBC_NAMESPACE_DECL
