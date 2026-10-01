//===-- MMIX Linux unwind cache locking -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_RWMUTEX_HPP
#define LIBUNWIND_MMIX_RWMUTEX_HPP

#include <pthread.h>
#include <stdlib.h>

namespace libunwind {

// Serialize FDE readers as well as writers using the available private mutex.
// No lazy initialization, allocation, weak fallback or pthread rwlock is needed.
class _LIBUNWIND_HIDDEN RWMutex {
public:
  bool lock_shared() { return lock(); }
  bool unlock_shared() { return unlock(); }
  bool lock() { return check(pthread_mutex_lock(&_lock)); }
  bool unlock() { return check(pthread_mutex_unlock(&_lock)); }

private:
  static bool check(int result) {
    // The caller logs failures but still accesses the cache; fail closed here.
    if (result != 0)
      abort();
    return true;
  }
  pthread_mutex_t _lock = PTHREAD_MUTEX_INITIALIZER;
};

} // namespace libunwind

#endif
