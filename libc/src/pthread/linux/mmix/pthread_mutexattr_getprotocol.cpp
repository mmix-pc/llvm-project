//===-- MMIX Linux mutex protocol attribute ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_mutexattr_getprotocol.h"
#include "hdr/errno_macros.h"
#include "mutex_protocol.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_mutexattr_getprotocol,
                   (const pthread_mutexattr_t *__restrict attr,
                    int *__restrict protocol)) {
  int value = mmix::mutex_protocol(*attr);
  if (value > PTHREAD_PRIO_PROTECT)
    return EINVAL;
  *protocol = value;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
