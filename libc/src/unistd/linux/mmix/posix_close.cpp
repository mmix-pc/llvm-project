//===-- MMIX Linux implementation of posix_close ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/posix_close.h"
#include "src/unistd/close.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_close, (int fd, int flag)) {
  // Invalid flags still consume the descriptor; a close error takes priority.
  int result = LIBC_NAMESPACE::close(fd);
  if (result == 0 && flag != 0) {
    libc_errno = EINVAL;
    return -1;
  }
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
