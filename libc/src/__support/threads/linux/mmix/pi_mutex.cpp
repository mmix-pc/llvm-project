//===-- MMIX Linux PI mutex kernel operations -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "robust_mutex.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
MutexError RobustMutex::acquire_pi(cpp::optional<Futex::Timeout> timeout,
                                   bool try_only) {
  int operation = try_only ? FUTEX_TRYLOCK_PI : FUTEX_LOCK_PI2;
  if (!is_robust() && !is_shared())
    operation |= FUTEX_PRIVATE_FLAG;
  const timespec *deadline = nullptr;
  if (!try_only && timeout) {
    deadline = &timeout->get_timespec();
    if (timeout->is_realtime())
      operation |= FUTEX_CLOCK_REALTIME;
  }
  for (;;) {
    long result =
        syscall_impl<long>(SYS_futex, &futex.val, operation, 0, deadline, 0, 0);
    if (result == 0) {
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      return MutexError::NONE;
    }
    if (result == -EINTR)
      continue;
    if (result == -ETIMEDOUT)
      return MutexError::TIMEOUT;
    if (result == -EDEADLK)
      return try_only ? MutexError::BUSY : MutexError::DEADLOCK;
    if (result == -EAGAIN || result == -EBUSY) {
      if (try_only)
        return MutexError::BUSY;
      continue;
    }
    return MutexError::BAD_LOCK_STATE;
  }
}

MutexError RobustMutex::release_pi() {
  // Publish all protected data before the kernel hands ownership to a waiter.
  __atomic_thread_fence(__ATOMIC_RELEASE);
  int operation = FUTEX_UNLOCK_PI;
  if (!is_robust() && !is_shared())
    operation |= FUTEX_PRIVATE_FLAG;
  long result =
      syscall_impl<long>(SYS_futex, &futex.val, operation, 0, 0, 0, 0);
  if (result == 0)
    return MutexError::NONE;
  if (result == -EPERM)
    return MutexError::UNLOCK_WITHOUT_LOCK;
  return MutexError::BAD_LOCK_STATE;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
