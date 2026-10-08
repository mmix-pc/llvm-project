//===-- MMIX Linux robust mutex ownership ------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ROBUST_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ROBUST_MUTEX_H

#include "mutex_flags.h"
#include "src/__support/threads/mutex_common.h"
#include "src/__support/threads/raw_mutex.h"
#include <linux/futex.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

int register_robust_list(bool force = false);

// The stalled owner slot and its padding become a robust link. The kernel TID
// is in the futex word; no process-local pointer is needed to find its owner.
struct RobustMutex {
  Futex futex;
  cpp::Atomic<uint32_t> flags;
  robust_list link;
  size_t lock_count;

  constexpr RobustMutex(bool recurse, bool shared, bool check,
                        bool inherit = false, bool recoverable = true)
      : futex(0),
        flags((inherit ? MUTEX_INHERIT : 0) | (recurse ? MUTEX_RECURSIVE : 0) |
              (recoverable ? MUTEX_ROBUST : 0) | (shared ? MUTEX_SHARED : 0) |
              (check ? MUTEX_ERRORCHECK : 0)),
        link{nullptr}, lock_count(0) {}
  bool has_flag(uint32_t mask) const { return mutex_flags(this) & mask; }
  void set_flag(uint32_t mask, bool value) {
    if (value)
      flags.fetch_or(mask, cpp::MemoryOrder::RELAXED);
    else
      flags.fetch_and(~mask, cpp::MemoryOrder::RELAXED);
  }
  bool is_priority_inherit() const { return has_flag(MUTEX_INHERIT); }
  bool is_recursive() const { return has_flag(MUTEX_RECURSIVE); }
  bool is_robust() const { return has_flag(MUTEX_ROBUST); }
  bool is_shared() const { return has_flag(MUTEX_SHARED); }
  bool is_error_checking() const { return has_flag(MUTEX_ERRORCHECK); }
  bool is_inconsistent() const { return has_flag(MUTEX_INCONSISTENT); }
  bool is_unrecoverable() const { return has_flag(MUTEX_UNRECOVERABLE); }
  MutexError acquire(cpp::optional<Futex::Timeout> timeout, bool try_only);
  MutexError acquire_pi(cpp::optional<Futex::Timeout> timeout, bool try_only);
  MutexError release_pi();
  MutexError lock() { return acquire(cpp::nullopt, false); }
  MutexError try_lock() { return acquire(cpp::nullopt, true); }
  MutexError timed_lock(Futex::Timeout timeout) {
    return acquire(timeout, false);
  }
  MutexError unlock();
  int consistent();
  int destroy();
  bool can_be_requeued() const { return false; }
  Futex &get_raw_futex() { return futex; }
};
static_assert(sizeof(RobustMutex) == 24 && alignof(RobustMutex) == 8);
static_assert(__builtin_offsetof(RobustMutex, futex) == 0);
static_assert(__builtin_offsetof(RobustMutex, flags) == 4);
static_assert(__builtin_offsetof(RobustMutex, link) == 8);
static_assert(__builtin_offsetof(RobustMutex, lock_count) == 16);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
