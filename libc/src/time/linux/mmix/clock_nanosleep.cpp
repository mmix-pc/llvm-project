//===-- MMIX Linux clock-selected sleep -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/clock_nanosleep.h"
#include "hdr/errno_macros.h"
#include "hdr/time_macros.h"
#include "src/__support/common.h"
#ifdef LIBC_MMIX_CLOCK_SLEEP_CANCELLATION
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#else
#include "src/__support/OSUtil/syscall.h"
#endif
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, clock_nanosleep,
                   (clockid_t clock, int flags, const timespec *request,
                    timespec *remaining)) {
  static_assert(sizeof(timespec) == 16);
#ifdef LIBC_MMIX_CLOCK_SLEEP_CANCELLATION
  long result = mmix::cancellation_syscall(
#else
  long result = syscall_impl<long>(
#endif
      SYS_clock_nanosleep, clock, flags, reinterpret_cast<long>(request),
      reinterpret_cast<long>(remaining));
  // Linux has no sleep operation for this constant and returns EOPNOTSUPP.
  // POSIX requires EINVAL for the calling thread's own CPU clock. Adapt only
  // this case, after the ordinary cancellation-point admission.
  if (clock == CLOCK_THREAD_CPUTIME_ID && result == -EOPNOTSUPP)
    return EINVAL;
  // Unlike nanosleep, return the error number directly without setting errno.
  return result < 0 ? static_cast<int>(-result) : static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
