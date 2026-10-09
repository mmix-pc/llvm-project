//===-- MMIX Linux thread CPU clocks ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_CLOCK_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_CLOCK_H

#include "hdr/errno_macros.h"
#include "hdr/types/clockid_t.h"
#include "src/__support/CPP/bit.h"

namespace LIBC_NAMESPACE_DECL {
struct ThreadAttributes;
namespace mmix {
inline int encode_thread_clock(int tid, clockid_t &clock) {
  static_assert(sizeof(clockid_t) == sizeof(uint32_t));
  static_assert(cpp::numeric_limits<clockid_t>::is_signed);
  if (tid <= 0)
    return ESRCH;
  // Linux recovers TID with an arithmetic right shift of the signed clock ID.
  if (tid > (cpp::numeric_limits<int32_t>::max() >> 3))
    return EOVERFLOW;
  constexpr uint32_t CPUClockSched = 2, CPUClockPerThread = 4;
  uint32_t bits =
      (~static_cast<uint32_t>(tid) << 3) | CPUClockPerThread | CPUClockSched;
  clock = cpp::bit_cast<clockid_t>(bits);
  return 0;
}
int probe_thread_clock(clockid_t clock);
int thread_clock(ThreadAttributes *handle, clockid_t &clock);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
