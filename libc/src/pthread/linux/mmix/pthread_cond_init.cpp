//===-- MMIX Linux implementation of pthread_cond_init --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_cond_init.h"
#include "hdr/errno_macros.h"
#include "hdr/time_macros.h"
#include "include/llvm-libc-macros/pthread-macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/threads/CndVar.h"

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(CndVar) == sizeof(pthread_cond_t) &&
              alignof(CndVar) == alignof(pthread_cond_t));

LLVM_LIBC_FUNCTION(int, pthread_cond_init,
                   (pthread_cond_t *__restrict cond,
                    const pthread_condattr_t *__restrict attr)) {
  LIBC_CRASH_ON_NULLPTR(cond);
  pthread_condattr_t value = {CLOCK_REALTIME, PTHREAD_PROCESS_PRIVATE};
  if (attr)
    value = *attr;
  if ((value.clock != CLOCK_REALTIME && value.clock != CLOCK_MONOTONIC) ||
      (value.pshared != PTHREAD_PROCESS_PRIVATE &&
       value.pshared != PTHREAD_PROCESS_SHARED))
    return EINVAL;
  // Do not construct a usable-looking object for an unqualified shared mode.
  if (value.pshared == PTHREAD_PROCESS_SHARED)
    return ENOTSUP;
  new (cond) CndVar(false, value.clock == CLOCK_REALTIME);
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
