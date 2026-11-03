//===-- MMIX Linux sigwait ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigwait.h"
#include "timed_wait.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sigwait,
                   (const sigset_t *__restrict set, int *__restrict sig)) {
  bool rejected;
  long result;
  do {
    result = mmix::timed_signal_wait(set, nullptr, nullptr, rejected);
  } while (result == -EINTR && !rejected);
  if (result < 0)
    return static_cast<int>(-result);
  *sig = static_cast<int>(result);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
