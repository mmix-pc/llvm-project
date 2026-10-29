//===-- posix_spawnattr_setschedparam implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawnattr_setschedparam.h"

#include "hdr/types/struct_sched_param.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnattr_setschedparam,
                   (posix_spawnattr_t *__restrict attr,
                    const struct sched_param *__restrict schedparam)) {
  // Priority validity depends on the policy applied in the child, which need
  // not be the policy currently stored in this object.
  attr->__priority = schedparam->sched_priority;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
