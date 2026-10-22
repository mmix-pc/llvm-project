//===-- MMIX Linux implementation of close ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, close, (int fd)) {
  auto result = linux_syscalls::close(fd);
  if (!result.has_value()) {
    // Linux has already detached the descriptor when close returns EINTR.
    libc_errno = result.error() == EINTR ? EINPROGRESS : result.error();
    return -1;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
