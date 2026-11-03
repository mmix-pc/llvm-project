//===-- MMIX Linux terminal drain ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/termios/tcdrain.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif
#include <asm/ioctls.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, tcdrain, (int fd)) {
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  long result = mmix::cancellation_syscall(SYS_ioctl, fd, TCSBRK, 1);
#else
  long result = syscall_impl<long>(SYS_ioctl, fd, TCSBRK, 1);
#endif
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
