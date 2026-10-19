//===-- MMIX Linux private signal filtering -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/raise.h"
#include "hdr/errno_macros.h"
#include "private_signals.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/raise.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, raise, (int sig)) {
  if (mmix::private_signal(sig)) {
    libc_errno = EINVAL;
    return -1;
  }
  auto result = linux_syscalls::raise(sig);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}
} // namespace LIBC_NAMESPACE_DECL
