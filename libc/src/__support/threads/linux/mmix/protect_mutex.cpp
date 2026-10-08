//===-- MMIX Linux priority-protection mutex ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "protect_mutex.h"
#include "cancellation_point.h"
#include "priority_protection_state.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/threads/identifier.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
class Operation {
  uint64_t previous = 0;

public:
  Operation() {
    uint64_t mask = CANCEL_MASK;
    if (syscall_impl<long>(SYS_rt_sigprocmask, 0, &mask, &previous, 8))
      __builtin_trap();
  }
  ~Operation() {
    if (syscall_impl<long>(SYS_rt_sigprocmask, 2, &previous, 0, 8))
      __builtin_trap();
  }
};
int withdraw(int ceiling, int original) {
  int error = priority_leave(ceiling);
  return error ? error : original;
}
} // namespace
int ProtectMutex::acquire(cpp::optional<Futex::Timeout> timeout,
                          bool try_only) {
  Operation operation;
  pid_t tid = internal::gettid();
  if (owner.load(cpp::MemoryOrder::RELAXED) == tid) {
    if (has_flag(MUTEX_RECURSIVE)) {
      if (lock_count == cpp::numeric_limits<size_t>::max())
        return EAGAIN;
      ++lock_count;
      return 0;
    }
    if (try_only)
      return EBUSY;
    if (has_flag(MUTEX_ERRORCHECK))
      return EDEADLK;
  }
  int reserved = ceiling();
  if (int error = priority_enter(reserved))
    return error;
  bool shared = has_flag(MUTEX_SHARED);
  bool acquired =
      try_only ? RawMutex::try_lock() : RawMutex::lock(timeout, shared);
  if (!acquired)
    return withdraw(reserved, try_only ? EBUSY : ETIMEDOUT);
  int actual = ceiling();
  if (actual != reserved) {
    // A setter may change the ceiling while we wait. Replace the reservation
    // only after ownership; do not retain a stale priority contribution.
    auto &state = priority_protection_state();
    int error =
        state.base > actual ? EINVAL : priority_replace(reserved, actual);
    if (error) {
      RawMutex::unlock(shared);
      return withdraw(reserved, error);
    }
  }
  owner.store(tid, cpp::MemoryOrder::RELAXED);
  lock_count = 1;
  return 0;
}
int ProtectMutex::release() {
  Operation operation;
  if (owner.load(cpp::MemoryOrder::RELAXED) != internal::gettid())
    return EPERM;
  if (lock_count > 1) {
    --lock_count;
    return 0;
  }
  int previous = ceiling();
  bool shared = has_flag(MUTEX_SHARED);
  lock_count = 0;
  owner.store(0, cpp::MemoryOrder::RELAXED);
  if (!RawMutex::unlock(shared))
    __builtin_trap();
  // No object access after handoff. Failure reports an already-unlocked mutex
  // and a retryable per-thread priority debt, not ownership rollback.
  return priority_leave(previous);
}
int ProtectMutex::set_ceiling(int value, int *previous) {
  int low, high;
  if (int error = priority_range(low, high))
    return error;
  if (value < low || value > high)
    return EINVAL;
  Operation operation;
  pid_t tid = internal::gettid();
  bool own = owner.load(cpp::MemoryOrder::RELAXED) == tid;
  if (own && !has_flag(MUTEX_RECURSIVE))
    return EDEADLK;
  bool shared = has_flag(MUTEX_SHARED);
  // POSIX permits the setter's implicit lock to bypass protection admission.
  if (!own)
    RawMutex::lock(cpp::nullopt, shared);
  int old = ceiling();
  uint32_t updated =
      (flags.load(cpp::MemoryOrder::RELAXED) & ~MUTEX_CEILING_MASK) |
      (uint32_t(value) << MUTEX_CEILING_SHIFT);
  int error = 0;
  if (own && value > old)
    error = priority_replace(old, value);
  if (!error) {
    flags.store(updated, cpp::MemoryOrder::RELAXED);
    if (own && value < old) {
      error = priority_replace(old, value);
      if (error)
        flags.store((updated & ~MUTEX_CEILING_MASK) |
                        (uint32_t(old) << MUTEX_CEILING_SHIFT),
                    cpp::MemoryOrder::RELAXED);
    }
  }
  if (!own)
    RawMutex::unlock(shared);
  if (error)
    return error;
  *previous = old;
  return 0;
}
int ProtectMutex::destroy() {
  return owner.load(cpp::MemoryOrder::RELAXED) || get_raw_futex().load() ? EBUSY
                                                                         : 0;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
