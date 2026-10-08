//===-- MMIX Linux cancelable condition waits -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_CANCELLATION_CONDITION_H
#define LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_CANCELLATION_CONDITION_H

#include "src/__support/threads/linux/mmix/cancellation.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/pthread/pthread_cond_utils.h"
#include <linux/futex.h>
#include <sys/syscall.h>
#ifdef LIBC_MMIX_ROBUST_MUTEX
#include "src/__support/threads/linux/mmix/robust_mutex.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
template <typename WaitMutex>
inline int condition_wait_impl(CndVar *cond, WaitMutex *mutex,
                               cpp::optional<CndVar::Timeout> timeout) {
  auto wait = [](RawMutex &barrier, cpp::optional<CndVar::Timeout> deadline) {
    constexpr FutexWordType UNLOCKED = 0, IN_CONTENTION = 2;
    auto &futex = barrier.get_raw_futex();
    for (;;) {
      test_cancel();
      if (futex.exchange(IN_CONTENTION, cpp::MemoryOrder::ACQUIRE) == UNLOCKED)
        return true;
      int operation = FUTEX_WAIT_BITSET_PRIVATE;
      const timespec *ts = nullptr;
      if (deadline) {
        ts = &deadline->get_timespec();
        if (deadline->is_realtime())
          operation |= FUTEX_CLOCK_REALTIME;
      }
      long ret = cancellation_syscall(
          SYS_futex, reinterpret_cast<long>(&futex.val), operation,
          IN_CONTENTION, reinterpret_cast<long>(ts), 0, FUTEX_BITSET_MATCH_ANY);
      if (ret == -ETIMEDOUT) {
        test_cancel();
        return false;
      }
      if (ret != 0 && ret != -EINTR && ret != -EAGAIN)
        __builtin_trap();
    }
  };
  auto shared_wait =
      [](Futex &futex, FutexWordType expected,
         cpp::optional<CndVar::Timeout> deadline) -> ErrorOr<int> {
    for (;;) {
      test_cancel();
      int operation = FUTEX_WAIT_BITSET;
      const timespec *ts = nullptr;
      if (deadline) {
        ts = &deadline->get_timespec();
        if (deadline->is_realtime())
          operation |= FUTEX_CLOCK_REALTIME;
      }
      long ret = cancellation_syscall(
          SYS_futex, reinterpret_cast<long>(&futex.val), operation, expected,
          reinterpret_cast<long>(ts), 0, FUTEX_BITSET_MATCH_ANY);
      if (ret == 0 || ret == -EAGAIN)
        return 0;
      if (ret == -ETIMEDOUT)
        return cpp::unexpected(ETIMEDOUT);
      if (ret != -EINTR)
        __builtin_trap();
    }
  };
  switch (cond->wait_with(
      mutex, timeout, wait, [] { test_cancel(); }, shared_wait)) {
  case CndVarResult::Success:
    return 0;
  case CndVarResult::Timeout:
    return ETIMEDOUT;
  case CndVarResult::MutexError:
    return EINVAL;
  case CndVarResult::OwnerDead:
    return EOWNERDEAD;
  case CndVarResult::NotRecoverable:
    return ENOTRECOVERABLE;
  }
  __builtin_unreachable();
}

inline int condition_wait(CndVar *cond, pthread_mutex_t *mutex,
                          cpp::optional<CndVar::Timeout> timeout) {
#ifdef LIBC_MMIX_ROBUST_MUTEX
  if (mutex->__robust || mutex->__priority_inherit)
    return condition_wait_impl(cond, reinterpret_cast<RobustMutex *>(mutex),
                               timeout);
#endif
  return condition_wait_impl(cond, pthread_cond_utils::to_mutex(mutex),
                             timeout);
}

inline int condition_timed_wait(CndVar *cond, pthread_mutex_t *mutex,
                                const timespec *abstime, bool realtime) {
  LIBC_CRASH_ON_NULLPTR(abstime);
  auto timeout = internal::AbsTimeout::from_timespec(*abstime, realtime);
  if (!timeout) {
    if (timeout.error() == internal::AbsTimeout::Error::Invalid)
      return EINVAL;
    timeout = internal::AbsTimeout::from_timespec({0, 0}, realtime);
  }
  return condition_wait(cond, mutex, timeout.value());
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
