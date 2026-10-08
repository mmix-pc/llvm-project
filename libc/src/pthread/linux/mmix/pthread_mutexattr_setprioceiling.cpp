//===-- MMIX Linux priority ceiling interface -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_mutexattr_setprioceiling.h"
#include "hdr/errno_macros.h"
#include "mutex_protocol.h"
#include "src/__support/threads/linux/mmix/priority_protection_state.h"
#include "src/__support/threads/linux/mmix/protect_mutex.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutexattr_setprioceiling,
                   (pthread_mutexattr_t * attr, int value)) {
  int low, high;
  if (int error = mmix::priority_range(low, high))
    return error;
  if (value < low || value > high)
    return EINVAL;
  *attr = (*attr & ~mmix::MUTEX_ATTR_CEILING_MASK) |
          (unsigned(value) << mmix::MUTEX_ATTR_CEILING_SHIFT);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
