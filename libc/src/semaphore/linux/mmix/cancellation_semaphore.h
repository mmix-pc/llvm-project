//===-- MMIX cancelable semaphore waits -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SEMAPHORE_LINUX_MMIX_CANCELLATION_SEMAPHORE_H
#define LLVM_LIBC_SRC_SEMAPHORE_LINUX_MMIX_CANCELLATION_SEMAPHORE_H

#include "src/__support/CPP/scope.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/semaphore/linux/semaphore.h"
#include <linux/futex.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

inline ErrorOr<int> semaphore_wait(Futex &futex,
                                   cpp::optional<internal::AbsTimeout> timeout,
                                   bool shared) {
  static_assert(sizeof(futex.val) == 4 && alignof(Futex) >= 4);
  static_assert(__atomic_always_lock_free(sizeof(FutexWordType), nullptr));
  // A canceled waiter may have consumed the kernel wake but not the token.
  // Hand the wake to another waiter before unwinding out of this operation.
  cpp::scope_exit handoff([&] { futex.notify_one(shared); });
  int operation = shared ? FUTEX_WAIT_BITSET : FUTEX_WAIT_BITSET_PRIVATE;
  const timespec *ts = nullptr;
  if (timeout) {
    ts = &timeout->get_timespec();
    if (timeout->is_realtime())
      operation |= FUTEX_CLOCK_REALTIME;
  }
  // The word is already a 32-bit object, not the low half of an octabyte.
  // Its address therefore needs no big-endian adjustment.
  long result = cancellation_syscall(
      SYS_futex, reinterpret_cast<long>(&futex.val), operation, 0,
      reinterpret_cast<long>(ts), 0, FUTEX_BITSET_MATCH_ANY);
  handoff.release();
  if (result < 0)
    return Error(static_cast<int>(-result));
  return static_cast<int>(result);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
