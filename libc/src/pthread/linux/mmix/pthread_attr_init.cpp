//===-- MMIX Linux pthread attribute initialization -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_attr_init.h"
#include "config/app.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/stack_policy.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_attr_init, (pthread_attr_t * attr)) {
  *attr = {PTHREAD_CREATE_JOINABLE,
           nullptr,
           mmix::DEFAULT_STACK_SIZE,
           app.page_size,
           0,
           0,
           PTHREAD_INHERIT_SCHED,
           PTHREAD_SCOPE_SYSTEM};
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
