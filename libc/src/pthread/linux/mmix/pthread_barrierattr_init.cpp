//===-- MMIX pthread_barrierattr_init implementation ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_barrierattr_init.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_barrierattr_init,
                   (pthread_barrierattr_t * attr)) {
  if (!attr)
    return EINVAL;
  attr->pshared = false;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
