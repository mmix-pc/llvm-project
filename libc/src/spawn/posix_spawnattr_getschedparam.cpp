//===-- posix_spawnattr_getschedparam implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawnattr_getschedparam.h"

#include "hdr/types/struct_sched_param.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnattr_getschedparam,
                   (const posix_spawnattr_t *__restrict attr,
                    struct sched_param *__restrict schedparam)) {
  schedparam->sched_priority = attr->__priority;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
