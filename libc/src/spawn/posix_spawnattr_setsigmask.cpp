//===-- posix_spawnattr_setsigmask implementation --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawnattr_setsigmask.h"

#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnattr_setsigmask,
                   (posix_spawnattr_t *__restrict attr,
                    const sigset_t *__restrict sigmask)) {
  // Runtime-owned signal filtering belongs to child attribute application.
  attr->__sigmask = *sigmask;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
