//===-- MMIX Linux sigpending implementation ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigpending.h"

#include "hdr/signal_macros.h"
#include "private_signals.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(unsigned long) == 8 && sizeof(sigset_t) == 128);
static_assert(NSIG == 65);

LLVM_LIBC_FUNCTION(int, sigpending, (sigset_t * set)) {
  // Stage the kernel word so failures cannot modify the public output.
  unsigned long pending = 0;
  long result = syscall_impl<long>(SYS_rt_sigpending, set ? &pending : nullptr,
                                   sizeof(pending));
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  if (set) {
    sigset_t output{};
    output.__signals[0] = pending & ~mmix::private_signal_mask;
    *set = output;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
