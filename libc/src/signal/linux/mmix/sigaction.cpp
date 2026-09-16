//===-- MMIX Linux signal registration ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigaction.h"
#include "rt_sigaction.h"
#include "signal_trampoline.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

// FIXME: Reuse shared signal policy after it separates the restorer/vDSO
// strategy. The single-thread profile does not yet provide the concurrent
// SIGABRT disposition and fork/spawn coordination required by pthread support.
LLVM_LIBC_FUNCTION(int, sigaction,
                   (int signal, const struct sigaction *__restrict action,
                    struct sigaction *__restrict old_action)) {
  if (signal <= 0 || signal >= NSIG) {
    libc_errno = EINVAL;
    return -1;
  }
  struct sigaction prepared{};
  if (action) {
    prepared = *action;
    if (!(prepared.sa_flags & SA_RESTORER)) {
      prepared.sa_flags |= SA_RESTORER;
      prepared.sa_restorer = __llvm_libc_mmix_signal_trampoline;
    }
  }
  auto result = mmix::rt_sigaction(signal, action ? &prepared : nullptr, old_action);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
