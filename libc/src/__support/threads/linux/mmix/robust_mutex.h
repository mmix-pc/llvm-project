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
  unsigned priority_inherit : 1;
  unsigned recursive : 1;
  unsigned robust : 1;
  unsigned pshared : 1;
  unsigned error_checking : 1;
  unsigned inconsistent : 1;
  unsigned unrecoverable : 1;
  robust_list link;
  size_t lock_count;

  constexpr RobustMutex(bool recurse, bool shared, bool check,
                        bool inherit = false, bool recoverable = true)
      : futex(0), priority_inherit(inherit), recursive(recurse),
        robust(recoverable), pshared(shared), error_checking(check),
        inconsistent(0), unrecoverable(0), link{nullptr}, lock_count(0) {}
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
static_assert(__builtin_offsetof(RobustMutex, link) == 8);
static_assert(__builtin_offsetof(RobustMutex, lock_count) == 16);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
