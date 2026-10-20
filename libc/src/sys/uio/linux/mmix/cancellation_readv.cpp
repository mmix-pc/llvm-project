//===-- MMIX Linux cancellable readv -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/uio/readv.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(ssize_t, readv, (int fd, const struct iovec *iov, int count)) {
  long result = mmix::cancellation_syscall(
      SYS_readv, fd, reinterpret_cast<long>(iov), count);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  size_t remaining = static_cast<size_t>(result);
  for (int i = 0; i < count && remaining; ++i) {
    size_t size = iov[i].iov_len < remaining ? iov[i].iov_len : remaining;
    LIBC_MSAN_UNPOISON(iov[i].iov_base, size);
    remaining -= size;
  }
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
