//===-- MMIX Linux pseudo-terminal allocation -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/posix_openpt.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/open.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, posix_openpt, (int flags)) {
  if ((flags & O_ACCMODE) != O_RDWR ||
      (flags & ~(O_RDWR | O_NOCTTY | O_CLOEXEC))) {
    libc_errno = EINVAL;
    return -1;
  }
  // The selected open provider owns cancellation and atomic fd flags. Do not
  // add a cancellation check after it has returned the caller's descriptor.
  return LIBC_NAMESPACE::open("/dev/ptmx", flags);
}
} // namespace LIBC_NAMESPACE_DECL
