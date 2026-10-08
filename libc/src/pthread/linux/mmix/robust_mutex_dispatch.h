//===-- MMIX Linux robust mutex dispatch -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_ROBUST_MUTEX_DISPATCH_H
#define LLVM_LIBC_SRC_PTHREAD_LINUX_MMIX_ROBUST_MUTEX_DISPATCH_H
#include "src/__support/threads/linux/mmix/robust_mutex.h"
#include "src/__support/threads/mutex.h"
#ifdef LIBC_MMIX_PROTECT_MUTEX
#include "src/__support/threads/linux/mmix/protect_mutex.h"
#endif
#include <pthread.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
inline int mutex_result(MutexError result, bool try_only = false) {
  switch (result) {
  case MutexError::NONE:
    return 0;
  case MutexError::OWNER_DEAD:
    return EOWNERDEAD;
  case MutexError::NOT_RECOVERABLE:
    return ENOTRECOVERABLE;
  case MutexError::BUSY:
    return EBUSY;
  case MutexError::DEADLOCK:
    return try_only ? EBUSY : EDEADLK;
  case MutexError::OVERFLOW:
    return EAGAIN;
  case MutexError::TIMEOUT:
    return ETIMEDOUT;
  case MutexError::UNLOCK_WITHOUT_LOCK:
    return EPERM;
  default:
    return EINVAL;
  }
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
