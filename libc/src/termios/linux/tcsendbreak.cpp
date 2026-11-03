//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Linux implementation of tcsendbreak.
///
//===----------------------------------------------------------------------===//

#include "src/termios/tcsendbreak.h"

#include "hdr/limits_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

#include <asm/ioctls.h> // Safe to include without the risk of name pollution.

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, tcsendbreak, (int fd, int duration)) {
  unsigned long request = TCSBRK;
  unsigned long units = 0;
  if (duration > 0) {
    units = static_cast<unsigned int>(duration) / 100 + (duration % 100 != 0);
    // TCSBRKP uses 100ms units, but the driver's break callback takes int ms.
    if (units > static_cast<unsigned int>(INT_MAX) / 100) {
      libc_errno = EOVERFLOW;
      return -1;
    }
    request = TCSBRKP;
  }
  auto ret = linux_syscalls::ioctl(fd, request, units);
  if (!ret.has_value()) {
    libc_errno = ret.error();
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
