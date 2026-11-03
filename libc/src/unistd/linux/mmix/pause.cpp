//===-- MMIX Linux pause implementation -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/pause.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/signal/sigsuspend.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pause, ()) {
  sigset_t mask{};
  long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, nullptr,
                                   &mask, 8);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  // MMIX has no pause syscall. Preserve the current mask, including private
  // notification state, and reuse the selected suspension/cancellation path.
  return LIBC_NAMESPACE::sigsuspend(&mask);
}
} // namespace LIBC_NAMESPACE_DECL
