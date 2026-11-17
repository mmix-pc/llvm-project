// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/sys/wait/wait3.h"
#include "src/sys/wait/wait4.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(pid_t, wait3, (int *status, int options, struct rusage *usage)) {
  return LIBC_NAMESPACE::wait4(-1, status, options, usage);
}
} // namespace LIBC_NAMESPACE_DECL
