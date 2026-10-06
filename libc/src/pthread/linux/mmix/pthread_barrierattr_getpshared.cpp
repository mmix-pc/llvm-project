//===-- MMIX pthread_barrierattr_getpshared implementation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_barrierattr_getpshared.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_barrierattr_getpshared,
                   (const pthread_barrierattr_t *__restrict attr,
                    int *__restrict pshared)) {
  if (!attr || !pshared)
    return EINVAL;
  *pshared = attr->pshared ? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
