//===-- Linux callonce fastpath -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_CALLONCE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_CALLONCE_H

#include "src/__support/macros/config.h"
#include "src/__support/threads/linux/futex_utils.h"

namespace LIBC_NAMESPACE_DECL {
using CallOnceFlag = Futex;

namespace callonce_impl {
static constexpr FutexWordType NOT_CALLED = 0x0;
static constexpr FutexWordType START = 0x11;
static constexpr FutexWordType WAITING = 0x22;
static constexpr FutexWordType FINISH = 0x33;

// Avoid cmpxchg operation if the function has already been called.
// The destination operand of cmpxchg may receive a write cycle without
// regard to the result of the comparison.
LIBC_INLINE bool callonce_fastpath(CallOnceFlag *flag) {
  return flag->load(cpp::MemoryOrder::ACQUIRE) == FINISH;
}

// An initializer owns the flag until it publishes completion or abandons it.
LIBC_INLINE void abandon(CallOnceFlag *flag) {
  if (flag->exchange(NOT_CALLED, cpp::MemoryOrder::RELEASE) == WAITING)
    flag->notify_all();
}

class CompletionGuard {
  CallOnceFlag *flag;

public:
  explicit CompletionGuard(CallOnceFlag *flag) : flag(flag) {}
  CompletionGuard(const CompletionGuard &) = delete;
  CompletionGuard &operator=(const CompletionGuard &) = delete;
  ~CompletionGuard() {
    if (flag)
      abandon(flag);
  }
  void complete() {
    auto *completed = flag;
    flag = nullptr;
    if (completed->exchange(FINISH, cpp::MemoryOrder::RELEASE) == WAITING)
      completed->notify_all();
  }
};

template <class CallOnceCallback>
[[gnu::noinline, gnu::cold]] int callonce_slowpath(CallOnceFlag *flag,
                                                   CallOnceCallback callback) {
  for (;;) {
    auto status = flag->load(cpp::MemoryOrder::ACQUIRE);
    if (status == FINISH)
      return 0;
    if (status == NOT_CALLED) {
      if (flag->compare_exchange_strong(status, START)) {
        CompletionGuard guard(flag);
        callback();
        guard.complete();
        return 0;
      }
      continue;
    }
    if (status == START) {
      if (!flag->compare_exchange_strong(status, WAITING))
        continue;
    }
    // A wakeup is not completion: retry both publication and owner election.
    flag->wait(WAITING);
  }
}
} // namespace callonce_impl

} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_CALLONCE_H
