//===-- MMIX Linux private signal filtering -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigtimedwait.h"
#include "timed_wait.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sigtimedwait,
                   (const sigset_t *__restrict set, siginfo_t *__restrict info,
                    const struct timespec *__restrict timeout)) {
  // Completed signal acceptance, including a copy-out failure, must not be
  // replaced by cancellation after the kernel has consumed the signal.
  bool rejected;
  long result = mmix::timed_signal_wait(set, info, timeout, rejected);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
