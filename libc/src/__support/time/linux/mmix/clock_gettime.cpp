//===-- MMIX Linux clock query --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/time/clock_gettime.h"
#include "src/__support/OSUtil/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<int> clock_gettime(clockid_t clock, timespec *value) {
  static_assert(sizeof(timespec::tv_sec) == 8 &&
                sizeof(timespec::tv_nsec) == 8 && sizeof(timespec) == 16);
  // FIXME: This syscall-only implementation is transitional. Reuse the common
  // vDSO-aware implementation once MMIX Linux and libc provide vDSO support.
  int result = syscall_impl<int>(SYS_clock_gettime, clock, value);
  if (result < 0)
    return Error(-result);
  return result;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
