//===-- MMIX Linux protected condition ownership -----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_PROTECT_CONDITION_H
#define LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_PROTECT_CONDITION_H

#include "src/__support/threads/CndVar.h"
#include "src/__support/threads/linux/mmix/protect_mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Callers validate ownership before registering with CndVar. A release error
// means ownership was dropped, so queue retirement and reacquisition still
// belong to CndVar; the wait policies below must not sleep on that path.
struct ProtectConditionMutex {
  ProtectMutex &mutex;
  int release_error = 0, acquire_error = 0;

  MutexError unlock() {
    release_error = mutex.release();
    return release_error ? MutexError::BAD_LOCK_STATE : MutexError::NONE;
  }
  MutexError lock() {
    acquire_error = mutex.acquire(cpp::nullopt, false, true);
    return acquire_error ? MutexError::BAD_LOCK_STATE : MutexError::NONE;
  }
  bool can_be_requeued() const { return false; }
  Futex &get_raw_futex() { return mutex.get_raw_futex(); }

  int result(CndVarResult result) const {
    if (acquire_error)
      return acquire_error;
    if (release_error)
      return release_error;
    switch (result) {
    case CndVarResult::Success:
      return 0;
    case CndVarResult::Timeout:
      return ETIMEDOUT;
    default:
      return EINVAL;
    }
  }
};

template <typename Wait, typename Check, typename SharedWait>
inline int protect_condition_wait(CndVar *cond, ProtectMutex *mutex,
                                  cpp::optional<CndVar::Timeout> timeout,
                                  Wait wait, Check check,
                                  SharedWait shared_wait) {
  if (!mutex->owned_by_current())
    return EPERM;
  ProtectConditionMutex view{*mutex};
  auto private_policy = [&](RawMutex &barrier,
                            cpp::optional<CndVar::Timeout> deadline) {
    return view.release_error ? false : wait(barrier, deadline);
  };
  auto shared_policy =
      [&](Futex &futex, FutexWordType expected,
          cpp::optional<CndVar::Timeout> deadline) -> ErrorOr<int> {
    if (view.release_error)
      return cpp::unexpected(view.release_error);
    return shared_wait(futex, expected, deadline);
  };
  CndVarResult result =
      cond->wait_with(&view, timeout, private_policy, check, shared_policy);
  return view.result(result);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
