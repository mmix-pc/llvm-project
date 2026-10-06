//===-- MMIX pthread_barrier_init implementation --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_barrier_init.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/barrier.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, pthread_barrier_init,
                   (pthread_barrier_t * barrier,
                    const pthread_barrierattr_t *__restrict attr,
                    unsigned count)) {
  if (!barrier || count == 0)
    return EINVAL;
  if (attr && attr->pshared)
    return ENOTSUP;
  // The common initializer still asserts on non-null attributes. Validated
  // private attributes are equivalent to its existing null/default selection.
  return Barrier::init(reinterpret_cast<Barrier *>(barrier), nullptr, count);
}

} // namespace LIBC_NAMESPACE_DECL
