//===-- Linux implementation of getdomainname ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/getdomainname.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/sys/utsname/uname.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, getdomainname, (char *name, size_t size)) {
  if (size && !name) {
    libc_errno = EFAULT;
    return -1;
  }
  utsname info;
  if (LIBC_NAMESPACE::uname(&info) < 0)
    return -1;
  // GNU/Linux permits a truncated result without a trailing terminator.
  for (size_t i = 0; i < size; ++i) {
    char value = info.domainname[i];
    name[i] = value;
    if (!value)
      break;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
