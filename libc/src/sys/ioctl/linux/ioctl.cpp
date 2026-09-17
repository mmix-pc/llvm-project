//===---------- Linux implementation of the ioctl function ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/ioctl/ioctl.h"

#include "hdr/sys_ioctl_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <stdarg.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ioctl, (int fd, unsigned long request, ...)) {
  va_list vargs;
  va_start(vargs, request);
  unsigned long arg = 0;
  switch (request) {
  case FIOCLEX:
  case FIONCLEX:
  case TIOCEXCL:
  case TIOCNXCL:
  case TIOCNOTTY:
  case TIOCCONS:
  case TIOCSBRK:
  case TIOCCBRK:
    // These requests do not consume a third argument.
    break;
  case TIOCSCTTY:
  case TCSBRK:
  case TCSBRKP:
  case TCXONC:
  case TCFLSH:
    // Legacy terminal commands take a promoted int, not a pointer-sized slot.
    arg = static_cast<unsigned long>(va_arg(vargs, int));
    break;
  default:
    // Preserve pointer transport for other requests. Legacy ioctl encodings
    // do not reliably describe argument types, so _IOC_SIZE is not sufficient.
    arg = reinterpret_cast<unsigned long>(va_arg(vargs, void *));
    break;
  }
  va_end(vargs);

  auto ret = linux_syscalls::ioctl(fd, request, arg);

  if (ret.has_value())
    return ret.value();

  libc_errno = ret.error();
  return -1;
}

} // namespace LIBC_NAMESPACE_DECL
