//===-- Linux implementation of sigsuspend --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigsuspend.h"

#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, sigsuspend, (const sigset_t *mask)) {
  // Overlay libc types can be larger than the kernel's signal set.
  constexpr unsigned long KERNEL_SIGSET_SIZE = (NSIG - 1) / 8;
  static_assert((NSIG - 1) % 8 == 0);
  static_assert(sizeof(sigset_t) >= KERNEL_SIGSET_SIZE);
  // Linux atomically installs the mask and waits. Do not retry EINTR: the
  // original mask has been restored after the handler returns.
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_rt_sigsuspend, mask,
                                              KERNEL_SIGSET_SIZE);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
