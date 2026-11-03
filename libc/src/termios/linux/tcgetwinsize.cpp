//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Linux implementation of tcgetwinsize.
///
//===----------------------------------------------------------------------===//

#include "src/termios/tcgetwinsize.h"

#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"

#include <asm/ioctls.h> // Safe to include without the risk of name pollution.

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, tcgetwinsize, (int fd, struct winsize *winsize_p)) {
  LIBC_CRASH_ON_NULLPTR(winsize_p);
  // Publish all four Linux ABI fields only after a successful ioctl.
  struct winsize size = {};
  auto ret = linux_syscalls::ioctl(fd, TIOCGWINSZ, &size);
  if (!ret.has_value()) {
    libc_errno = ret.error();
    return -1;
  }
  *winsize_p = size;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
