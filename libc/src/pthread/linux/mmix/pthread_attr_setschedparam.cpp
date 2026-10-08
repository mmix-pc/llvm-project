//===-- MMIX Linux pthread_attr_setschedparam -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_attr_setschedparam.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/priority_protection_state.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_attr_setschedparam,
                   (pthread_attr_t *__restrict attr,
                    const struct sched_param *__restrict value)) {
  if (int error =
          mmix::scheduling_validate(attr->__schedpolicy, value->sched_priority))
    return error;
  attr->__schedpriority = value->sched_priority;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
