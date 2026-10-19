//===-- MMIX Linux cancellable usleep -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/struct_timespec.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/unistd/usleep.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, usleep, (useconds_t usec)) {
  static_assert(sizeof(useconds_t) >= 4, "Avoids overflow in ns computation");
  constexpr useconds_t US_IN_S = 1'000'000;
  timespec request = {usec / US_IN_S, (usec % US_IN_S) * 1000};
  long result = mmix::cancellation_syscall(
      SYS_nanosleep, reinterpret_cast<long>(&request), 0);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
