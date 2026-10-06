//===-- MMIX pthread_barrierattr_destroy implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_barrierattr_destroy.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_barrierattr_destroy,
                   (pthread_barrierattr_t * attr)) {
  return attr ? 0 : EINVAL;
}
} // namespace LIBC_NAMESPACE_DECL
