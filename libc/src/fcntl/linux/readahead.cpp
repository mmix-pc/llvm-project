// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/fcntl/readahead.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
static_assert(sizeof(long) == 8 && sizeof(void *) == 8 && sizeof(off_t) == 8,
              "readahead requires a reviewed native LP64 syscall ABI");

LLVM_LIBC_FUNCTION(ssize_t, readahead, (int fd, off_t offset, size_t count)) {
  long result = LIBC_NAMESPACE::syscall_impl<long>(SYS_readahead, fd, offset, count);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<ssize_t>(result);
}
} // namespace LIBC_NAMESPACE_DECL
