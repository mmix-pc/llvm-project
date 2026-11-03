//===-- Implementation of sigwait -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigwait.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/signal/sigtimedwait.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sigwait,
                   (const sigset_t *__restrict set, int *__restrict sig)) {
  int result;
  do {
    result = LIBC_NAMESPACE::sigtimedwait(set, nullptr, nullptr);
  } while (result < 0 && libc_errno == EINTR);
  if (result < 0)
    return libc_errno;
  *sig = result;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
