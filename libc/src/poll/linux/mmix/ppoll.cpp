//===-- MMIX Linux ppoll signal mask --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/poll/ppoll.h"
#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/signal/linux/mmix/private_signals.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, ppoll,
                   (pollfd * fds, nfds_t count, const timespec *timeout,
                    const sigset_t *mask)) {
  timespec copy{};
  if (timeout) {
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
        timeout->tv_nsec >= 1000000000) {
      libc_errno = EINVAL;
      return -1;
    }
    copy = *timeout;
  }
  sigset_t filtered{};
  if (mask) {
    filtered = *mask;
    filtered.__signals[0] &= ~mmix::private_signal_mask;
  }
  long result =
      syscall_impl<long>(SYS_ppoll, fds, count, timeout ? &copy : nullptr,
                         mask ? &filtered : nullptr, 8);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
