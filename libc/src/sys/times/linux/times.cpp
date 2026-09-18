//===-- Linux implementation of times -------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/sys/times/times.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(clock_t, times, (struct tms * buffer)) {
  // Only the kernel's reserved errno range denotes failure, not every
  // clock_t with its sign bit set.
  auto result = linux_syscalls::syscall_checked<clock_t>(SYS_times, buffer);
  if (!result) {
    libc_errno = result.error();
    return static_cast<clock_t>(-1);
  }
  return result.value();
}
} // namespace LIBC_NAMESPACE_DECL
