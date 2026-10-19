//===-- MMIX Linux private signal filtering -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigsuspend.h"
#include "hdr/errno_macros.h"
#include "private_signals.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sigsuspend, (const sigset_t *mask)) {
  sigset_t filtered{};
  if (mask) {
    filtered = *mask;
    filtered.__signals[0] &= ~mmix::private_signal_mask;
  }
  long result =
      syscall_impl<long>(SYS_rt_sigsuspend, mask ? &filtered : nullptr, 8);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
