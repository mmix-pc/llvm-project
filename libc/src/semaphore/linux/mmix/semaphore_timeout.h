//===-- MMIX timed semaphore retry policy ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SEMAPHORE_LINUX_MMIX_SEMAPHORE_TIMEOUT_H
#define LLVM_LIBC_SRC_SEMAPHORE_LINUX_MMIX_SEMAPHORE_TIMEOUT_H

#include "src/__support/time/clock_gettime.h"
#include "src/semaphore/linux/semaphore.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct SemaphoreTimeoutPolicy {
  static constexpr bool RETRY_AFTER_TIMEOUT = false;

  LIBC_INLINE void on_abandon(Futex &value, bool shared) const {
    // A post may have selected this waiter before its deadline check. Pass
    // that wake onward when returning without consuming the posted token.
    value.notify_one(shared);
  }

  LIBC_INLINE int before_retry(cpp::optional<internal::AbsTimeout> timeout) const {
    // The initial immediate attempt and timeout validation precede this hook.
    // Do not consume a token after expiry, even following a benign wakeup.
    const timespec &limit = timeout->get_timespec();
    timespec now;
    auto result = internal::clock_gettime(
        timeout->is_realtime() ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
    if (!result)
      return result.error();
    return now.tv_sec > limit.tv_sec ||
                   (now.tv_sec == limit.tv_sec && now.tv_nsec >= limit.tv_nsec)
               ? ETIMEDOUT
               : 0;
  }
};

LIBC_INLINE ErrorOr<int>
semaphore_timed_futex_wait(Futex &value,
                           cpp::optional<internal::AbsTimeout> timeout,
                           bool shared) {
  return value.wait(0, timeout, shared, /*restart_on_interrupt=*/false);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
