//===-- Linux implementation of ppoll ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/poll/ppoll.h"

#include "hdr/limits_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ppoll,
                   (pollfd * fds, nfds_t nfds, const timespec *timeout,
                    const sigset_t *mask)) {
#if defined(SYS_ppoll_time64)
  using KernelTime = __INT64_TYPE__;
  constexpr long SYSCALL = SYS_ppoll_time64;
#elif defined(SYS_ppoll)
  using KernelTime = long;
  constexpr long SYSCALL = SYS_ppoll;
#else
#error "ppoll or ppoll_time64 syscall required"
#endif
  struct KernelTimespec {
    KernelTime seconds;
    KernelTime nanoseconds;
  } copy;
  if (timeout) {
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
        timeout->tv_nsec >= 1000000000) {
      libc_errno = EINVAL;
      return -1;
    }
#if !defined(SYS_ppoll_time64)
    if (timeout->tv_sec > LONG_MAX) {
      libc_errno = EOVERFLOW;
      return -1;
    }
#endif
    copy.seconds = static_cast<KernelTime>(timeout->tv_sec);
    copy.nanoseconds = static_cast<KernelTime>(timeout->tv_nsec);
  }
  // Linux can write back the remaining timeout, including on interruption.
  // Keep the public const input untouched and use the kernel signal-set size,
  // not a potentially larger overlay libc sigset_t.
  constexpr unsigned long KERNEL_SIGSET_SIZE = (NSIG - 1) / 8;
  static_assert((NSIG - 1) % 8 == 0);
  static_assert(sizeof(sigset_t) >= KERNEL_SIGSET_SIZE);
  int ret = LIBC_NAMESPACE::syscall_impl<int>(
      SYSCALL, fds, nfds, timeout ? &copy : nullptr, mask, KERNEL_SIGSET_SIZE);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
