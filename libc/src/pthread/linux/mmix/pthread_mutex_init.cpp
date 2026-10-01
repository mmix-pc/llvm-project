//===-- MMIX Linux implementation of pthread_mutex_init -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_mutex_init.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/common.h"
#include "src/__support/threads/mutex.h"
#include "src/pthread/pthread_mutexattr.h"

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(Mutex) == sizeof(pthread_mutex_t) &&
              alignof(Mutex) == alignof(pthread_mutex_t));

LLVM_LIBC_FUNCTION(int, pthread_mutex_init,
                   (pthread_mutex_t * mutex,
                    const pthread_mutexattr_t *__restrict attr)) {
  auto value = attr ? *attr : DEFAULT_MUTEXATTR;
  constexpr unsigned known = unsigned(PThreadMutexAttrPos::TYPE_MASK) |
                             unsigned(PThreadMutexAttrPos::ROBUST_MASK) |
                             unsigned(PThreadMutexAttrPos::PSHARED_MASK);
  if (value & ~known)
    return EINVAL;
  // Reject unqualified modes before touching the destination. Protocol has
  // no attribute encoding yet; unknown bits cannot silently select it.
  switch (get_mutexattr_type(value)) {
  case PTHREAD_MUTEX_NORMAL:
    break;
  case PTHREAD_MUTEX_RECURSIVE:
  case PTHREAD_MUTEX_ERRORCHECK:
    return ENOTSUP;
  default:
    return EINVAL;
  }
  if (get_mutexattr_robust(value) != PTHREAD_MUTEX_STALLED ||
      get_mutexattr_pshared(value) != PTHREAD_PROCESS_PRIVATE)
    return ENOTSUP;
  new (mutex) Mutex(false, false, false, false, false);
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
