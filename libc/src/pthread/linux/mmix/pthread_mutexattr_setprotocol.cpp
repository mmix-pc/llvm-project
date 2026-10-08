//===-- MMIX Linux mutex protocol attribute ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_mutexattr_setprotocol.h"
#include "hdr/errno_macros.h"
#include "mutex_protocol.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutexattr_setprotocol,
                   (pthread_mutexattr_t * attr, int protocol)) {
  if (protocol != PTHREAD_PRIO_NONE && protocol != PTHREAD_PRIO_INHERIT &&
      protocol != PTHREAD_PRIO_PROTECT)
    return EINVAL;
  if (protocol == PTHREAD_PRIO_PROTECT)
    return ENOTSUP;
  *attr = (*attr & ~mmix::MUTEX_PROTOCOL_MASK) |
          (unsigned(protocol) << mmix::MUTEX_PROTOCOL_SHIFT);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
