//===-- MMIX Linux sigaddset implementation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigaddset.h"

#include "hdr/signal_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

// FIXME: Reuse the common Linux provider once signal-set/mask operations are
// decoupled from signal_utils.h's handler, vDSO and locking dependencies.
namespace LIBC_NAMESPACE_DECL {

// Public capacity and the kernel transfer size are both one octa on MMIX.
static_assert(sizeof(unsigned long) == 8 && sizeof(sigset_t) == 8);
static_assert(NSIG == 65);

LLVM_LIBC_FUNCTION(int, sigaddset, (sigset_t * set, int signum)) {
  if (!set || signum <= 0 || signum >= NSIG) {
    libc_errno = EINVAL;
    return -1;
  }
  // Preserve the upstream Linux signal-number-to-bit mapping.
  unsigned bit = static_cast<unsigned>(signum - 1);
  set->__signals[bit / 64] |= (1UL << (bit % 64));
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
