//===-- MMIX Linux robust priority-protection mutex -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "robust_protect_mutex.h"
#include "cancellation_point.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "priority_protection_state.h"
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
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &mask, &previous,
                           sizeof(mask)))
      __builtin_trap();
  }
  ~Operation() {
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &previous, 0,
                           sizeof(previous)))
      __builtin_trap();
  }
};

int as_errno(MutexError error) {
  switch (error) {
  case MutexError::NONE:
    return 0;
  case MutexError::BUSY:
    return EBUSY;
  case MutexError::DEADLOCK:
    return EDEADLK;
  case MutexError::TIMEOUT:
    return ETIMEDOUT;
  case MutexError::UNLOCK_WITHOUT_LOCK:
    return EPERM;
  case MutexError::OVERFLOW:
    return EAGAIN;
  case MutexError::OWNER_DEAD:
    return EOWNERDEAD;
  case MutexError::NOT_RECOVERABLE:
    return ENOTRECOVERABLE;
  case MutexError::BAD_LOCK_STATE:
    return EINVAL;
  }
  __builtin_trap();
}

int withdraw(int ceiling, int original) {
  int error = priority_leave(ceiling);
  return error ? error : original;
}

// The caller masks cancellation across both transactions. Never enter the
// ledger while a base operation has its robust pending pointer installed.
int acquire_checked(RobustProtectMutex &mutex,
                    cpp::optional<Futex::Timeout> timeout, bool try_only,
                    bool check_base, bool condition_recovery) {
  if (mutex.owned_by_current())
    return as_errno(mutex.RobustMutex::acquire(timeout, try_only));
  int reserved = mutex.ceiling();
  if (int error = priority_enter(reserved, check_base, condition_recovery))
    return error;
  MutexError result = mutex.RobustMutex::acquire(timeout, try_only);
  if (!mutex_owns_lock(result))
    return withdraw(reserved, as_errno(result));
  // Revalidate even an unchanged ceiling: a peer can change base priority
  // during the wait. Validation and replacement share the ledger guard.
  int error = priority_replace_checked(reserved, mutex.ceiling(), check_base,
                                       condition_recovery);
  if (error) {
    mutex.abandon_unpublished();
    return withdraw(reserved, error);
  }
  return as_errno(result);
}
} // namespace

bool RobustProtectMutex::owned_by_current() {
  return (futex.load(cpp::MemoryOrder::RELAXED) & FUTEX_TID_MASK) ==
         uint32_t(internal::gettid());
}

int RobustProtectMutex::acquire(cpp::optional<Futex::Timeout> timeout,
                                bool try_only, bool condition_recovery) {
  Operation operation;
  return acquire_checked(*this, timeout, try_only, true, condition_recovery);
}

int RobustProtectMutex::release() {
  Operation operation;
  if (!owned_by_current())
    return EPERM;
  bool final = lock_count == 1;
  int previous = ceiling();
  MutexError result = RobustMutex::unlock();
  if (result != MutexError::NONE)
    return as_errno(result);
  // Only cached locals survive handoff; restoration errors mean no ownership.
  return final ? priority_leave(previous) : 0;
}

int RobustProtectMutex::set_ceiling(int value, int *previous) {
  int low, high;
  if (int error = priority_range(low, high))
    return error;
  if (value < low || value > high)
    return EINVAL;
  Operation operation;
  bool own = owned_by_current();
  if (own && !is_recursive())
    return EDEADLK;
  if (!own) {
    // Setter admission bypasses only the base-ceiling comparison. On owner
    // death, retain the acquired lock and contribution for caller recovery.
    int error = acquire_checked(*this, cpp::nullopt, false, false, false);
    if (error)
      return error;
  }
  int old = ceiling();
  // An implicit setter need not retain a contribution after returning. Finish
  // its fallible withdrawal before publishing the new ceiling, as the setter
  // may hold the base lock without protection admission. A failed withdrawal
  // still removes the contribution and leaves restoration debt, so use the
  // base unlock below rather than release().
  int error = own ? priority_replace_checked(old, value, false)
                  : priority_leave(old);
  if (!error) {
    uint32_t updated =
        (flags.load(cpp::MemoryOrder::RELAXED) & ~MUTEX_ROBUST_CEILING_MASK) |
        (uint32_t(value) << MUTEX_ROBUST_CEILING_SHIFT);
    flags.store(updated, cpp::MemoryOrder::RELAXED);
  }
  if (!own) {
    if (RobustMutex::unlock() != MutexError::NONE)
      __builtin_trap();
  }
  if (error)
    return error;
  *previous = old;
  return 0;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
