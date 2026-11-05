//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/thrd_sleep.h"
#include "src/__support/common.h"
#include "src/time/nanosleep.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, thrd_sleep,
                   (const timespec *duration, timespec *remaining)) {
  return LIBC_NAMESPACE::nanosleep(duration, remaining);
}

} // namespace LIBC_NAMESPACE_DECL
