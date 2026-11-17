// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/sys/timex/adjtimex.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
static_assert(sizeof(long) == 8 && sizeof(struct timex) == 208,
              "adjtimex requires the reviewed native LP64 timex ABI");
LLVM_LIBC_FUNCTION(int, adjtimex, (struct timex *buffer)) {
  long result = LIBC_NAMESPACE::syscall_impl<long>(SYS_adjtimex, buffer);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
