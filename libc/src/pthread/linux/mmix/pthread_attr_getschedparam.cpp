//===-- MMIX Linux pthread_attr_getschedparam -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_attr_getschedparam.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_attr_getschedparam,
                   (const pthread_attr_t *__restrict attr,
                    struct sched_param *__restrict value)) {
  value->sched_priority = attr->__schedpriority;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
