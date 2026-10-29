//===-- posix_spawnattr_setschedpolicy implementation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawnattr_setschedpolicy.h"

#include "hdr/errno_macros.h"
#include "hdr/sched_macros.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnattr_setschedpolicy,
                   (posix_spawnattr_t * attr, int schedpolicy)) {
  if (schedpolicy != SCHED_OTHER && schedpolicy != SCHED_FIFO &&
      schedpolicy != SCHED_RR)
    return EINVAL;
  attr->__policy = schedpolicy;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
