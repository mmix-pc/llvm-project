//===-- Linux signal-set emptiness query ----------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/signal/sigisemptyset.h"
#include "hdr/signal_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, sigisemptyset, (const sigset_t *set)) {
  if (!set) {
    libc_errno = EINVAL;
    return -1;
  }
  constexpr unsigned bits = sizeof(unsigned long) * 8;
  constexpr unsigned words = (NSIG - 1 + bits - 1) / bits;
  static_assert(words * sizeof(unsigned long) <= sizeof(sigset_t));
  for (unsigned i = 0; i < words; ++i) {
    unsigned long word;
    __builtin_memcpy(
        &word, reinterpret_cast<const unsigned char *>(set) + i * sizeof(word),
        sizeof(word));
    if constexpr ((NSIG - 1) % bits)
      if (i == words - 1)
        word &= (1UL << ((NSIG - 1) % bits)) - 1;
    if (word)
      return 0;
  }
  return 1;
}
} // namespace LIBC_NAMESPACE_DECL
