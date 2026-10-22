//===-- MMIX Linux pselect with public-to-kernel signal mask conversion ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/select/pselect.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/signal/linux/mmix/private_signals.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, pselect,
                   (int nfds, fd_set *__restrict read_set,
                    fd_set *__restrict write_set, fd_set *__restrict error_set,
                    const timespec *__restrict timeout,
                    const sigset_t *__restrict mask)) {
  timespec copy{};
  if (timeout) {
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
        timeout->tv_nsec >= 1000000000) {
      libc_errno = EINVAL;
      return -1;
    }
    copy = *timeout;
  }
  uint64_t filtered = mask ? mask->__signals[0] & ~mmix::private_signal_mask : 0;
  struct {
    const uint64_t *mask;
    unsigned long size;
  } kernel_mask{mask ? &filtered : nullptr, sizeof(filtered)};
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  long result = mmix::cancellation_syscall(
      SYS_pselect6, nfds, reinterpret_cast<long>(read_set),
      reinterpret_cast<long>(write_set), reinterpret_cast<long>(error_set),
      timeout ? reinterpret_cast<long>(&copy) : 0,
      reinterpret_cast<long>(&kernel_mask));
#else
  long result = syscall_impl<long>(SYS_pselect6, nfds, read_set, write_set,
                                   error_set, timeout ? &copy : nullptr,
                                   &kernel_mask);
#endif
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}

} // namespace LIBC_NAMESPACE_DECL
