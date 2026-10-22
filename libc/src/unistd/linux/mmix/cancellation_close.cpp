//===-- MMIX Linux cancellable close --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/unistd/close.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, close, (int fd)) {
  long result = mmix::cancellation_syscall(SYS_close, fd);
  if (result < 0) {
    // Translate only after the cancellation window has resolved raw fd state.
    libc_errno = result == -EINTR ? EINPROGRESS : static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
