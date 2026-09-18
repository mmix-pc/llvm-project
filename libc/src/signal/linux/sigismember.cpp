//===-- Linux implementation of sigismember -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigismember.h"

#include "hdr/signal_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, sigismember, (const sigset_t *set, int signal)) {
  if (!set || signal <= 0 || signal >= NSIG) {
    libc_errno = EINVAL;
    return -1;
  }
  constexpr unsigned BITS_PER_WORD = sizeof(unsigned long) * 8;
  unsigned bit = static_cast<unsigned>(signal - 1);
  unsigned long word;
  // Read the Linux signal-word representation without depending on a host
  // libc's private sigset_t member names in overlay builds.
  __builtin_memcpy(&word,
                   reinterpret_cast<const unsigned char *>(set) +
                       (bit / BITS_PER_WORD) * sizeof(word),
                   sizeof(word));
  return (word & (1UL << (bit % BITS_PER_WORD))) != 0;
}

} // namespace LIBC_NAMESPACE_DECL
