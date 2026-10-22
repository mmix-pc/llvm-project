//===-- MMIX Linux cancellable pread --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/pread.h"

#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, pread,
                   (int fd, void *buffer, size_t count, off_t offset)) {
  long result = mmix::cancellation_syscall(
      SYS_pread64, fd, reinterpret_cast<long>(buffer), count, offset);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  LIBC_MSAN_UNPOISON(buffer, static_cast<size_t>(result));
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
