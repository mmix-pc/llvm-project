//===-- MMIX Linux cancellable sleep --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/struct_timespec.h"
#include "hdr/types/time_t.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/unistd/sleep.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(unsigned int, sleep, (unsigned int seconds)) {
  static_assert(sizeof(unsigned int) <= sizeof(time_t), "Avoids overflow");
  timespec request = {seconds, 0};
  timespec remaining = {};
  long result = mmix::cancellation_syscall(
      SYS_nanosleep, reinterpret_cast<long>(&request),
      reinterpret_cast<long>(&remaining));
  return result < 0 ? static_cast<unsigned int>(remaining.tv_sec) : 0;
}
} // namespace LIBC_NAMESPACE_DECL
