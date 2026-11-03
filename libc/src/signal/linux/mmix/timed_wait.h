//===-- MMIX Linux signal wait dispatch --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_TIMED_WAIT_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_TIMED_WAIT_H

#include "private_signals.h"
#include "hdr/errno_macros.h"
#include "hdr/types/siginfo_t.h"
#include "hdr/types/sigset_t.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/OSUtil/syscall.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
inline long timed_signal_wait(const sigset_t *set, siginfo_t *info,
                              const timespec *timeout, bool &rejected) {
  rejected = false;
  if (timeout && (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                  timeout->tv_nsec >= 1000000000))
    return -EINVAL;
  sigset_t filtered{};
  if (set) {
    filtered = *set;
    filtered.__signals[0] &= ~private_signal_mask;
  }
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  auto result = cancellation_signal_wait(
      SYS_rt_sigtimedwait, reinterpret_cast<long>(set ? &filtered : nullptr),
      reinterpret_cast<long>(info), reinterpret_cast<long>(timeout), 8);
  rejected = result.cancellation_rejected;
  return result.value;
#else
  return syscall_impl<long>(SYS_rt_sigtimedwait, set ? &filtered : nullptr,
                            info, timeout, 8);
#endif
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
