//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/time/settimeofday.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, settimeofday,
                   (const struct timeval *tv, const struct timezone *tz)) {
  // SYS_settimeofday uses the legacy kernel timeval, with two signed longs.
  struct KernelTimeval {
    long sec;
    long usec;
  } value;
  if (tv) {
    if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
      libc_errno = EINVAL;
      return -1;
    }
    value.sec = static_cast<long>(tv->tv_sec);
    value.usec = static_cast<long>(tv->tv_usec);
    if (value.sec != tv->tv_sec) {
      libc_errno = EOVERFLOW;
      return -1;
    }
  }
  int ret = syscall_impl<int>(SYS_settimeofday, tv ? &value : nullptr, tz);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
