//===-- MMIX Linux CPU clock kernel query ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/types/struct_timespec.h"
#include "src/__support/OSUtil/syscall.h"
#include "thread_clock.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int probe_thread_clock(clockid_t clock) {
  timespec sample;
  static_assert(sizeof(sample) == 16 && sizeof(sample.tv_sec) == 8 &&
                sizeof(sample.tv_nsec) == 8);
  long result = syscall_impl<long>(SYS_clock_gettime, clock, &sample);
  return result < 0 ? int(-result) : 0;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
