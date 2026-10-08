//===-- MMIX Linux implementation of pthread_rwlock_init
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_rwlock_init.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/common.h"
#include "src/__support/threads/unix_rwlock.h"

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(RwLock) == sizeof(pthread_rwlock_t) &&
              alignof(RwLock) == alignof(pthread_rwlock_t));

LLVM_LIBC_FUNCTION(int, pthread_rwlock_init,
                   (pthread_rwlock_t * rwlock,
                    const pthread_rwlockattr_t *__restrict attr)) {
  if (!rwlock)
    return EINVAL;
  pthread_rwlockattr_t selected{PTHREAD_PROCESS_PRIVATE,
                                PTHREAD_RWLOCK_PREFER_READER_NP};
  if (attr)
    selected = *attr;
  rwlock::Role preference;
  switch (selected.pref) {
  case PTHREAD_RWLOCK_PREFER_READER_NP:
    preference = rwlock::Role::Reader;
    break;
  case PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP:
    preference = rwlock::Role::Writer;
    break;
  default:
    return EINVAL;
  }
#ifndef LIBC_MMIX_PROCESS_SHARED
  if (selected.pshared == PTHREAD_PROCESS_SHARED)
    return ENOTSUP;
#endif
  if (selected.pshared != PTHREAD_PROCESS_PRIVATE &&
      selected.pshared != PTHREAD_PROCESS_SHARED)
    return EINVAL;
  // Preserve the destination when attributes select an unsupported mode.
  new (rwlock) RwLock(preference, selected.pshared == PTHREAD_PROCESS_SHARED);
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
