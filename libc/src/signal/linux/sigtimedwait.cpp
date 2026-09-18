//===-- Linux implementation of sigtimedwait ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigtimedwait.h"

#include "hdr/limits_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, sigtimedwait,
                   (const sigset_t *__restrict set, siginfo_t *__restrict info,
                    const struct timespec *__restrict timeout)) {
#if defined(SYS_rt_sigtimedwait_time64)
  using KernelTime = __INT64_TYPE__;
  constexpr long SYSCALL = SYS_rt_sigtimedwait_time64;
#elif defined(SYS_rt_sigtimedwait)
  using KernelTime = long;
  constexpr long SYSCALL = SYS_rt_sigtimedwait;
#else
#error "rt_sigtimedwait or rt_sigtimedwait_time64 syscall required"
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
#if !defined(SYS_rt_sigtimedwait_time64)
    if (timeout->tv_sec > LONG_MAX) {
      libc_errno = EOVERFLOW;
      return -1;
    }
#endif
    copy.seconds = static_cast<KernelTime>(timeout->tv_sec);
    copy.nanoseconds = static_cast<KernelTime>(timeout->tv_nsec);
  }
  // Overlay signal sets may be larger than the kernel's. Let Linux consume
  // a pending signal atomically; preserve EINTR rather than retrying the wait.
  constexpr unsigned long KERNEL_SIGSET_SIZE = (NSIG - 1) / 8;
  static_assert((NSIG - 1) % 8 == 0);
  static_assert(sizeof(sigset_t) >= KERNEL_SIGSET_SIZE);
  int ret = LIBC_NAMESPACE::syscall_impl<int>(
      SYSCALL, set, info, timeout ? &copy : nullptr, KERNEL_SIGSET_SIZE);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
