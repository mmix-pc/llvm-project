//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Threading.h"

// Isolate platform lock algorithms from libc's SINGLE-mode class definitions.
// Do not import its full-build thread/TLS state; identity uses the real syscall.
#if defined(LIBC_FULL_BUILD) || defined(LIBC_THREAD_MODE) || defined(LIBC_NAMESPACE)
#  error "The MMIX libc++ primitive bridge requires an isolated libc configuration"
#endif
#define LIBC_NAMESPACE __llvm_libc_mmix_cxx_sync
#define LIBC_THREAD_MODE LIBC_THREAD_MODE_PLATFORM

#include "src/__support/CPP/new.h"
#include "src/__support/threads/CndVar.h"
#include "src/__support/threads/identifier.h"
#include <errno.h>

#ifdef LIBC_COPT_TIMEOUT_ENSURE_MONOTONICITY
#  error "The MMIX libc++ primitive bridge supports untimed synchronization"
#endif

namespace {
using LIBC_NAMESPACE::CndVar;
using LIBC_NAMESPACE::CndVarResult;
using LIBC_NAMESPACE::Mutex;
using LIBC_NAMESPACE::MutexError;

static_assert(sizeof(Mutex) == sizeof(mtx_t) && alignof(Mutex) == alignof(mtx_t));
static_assert(sizeof(CndVar) == sizeof(cnd_t) && alignof(CndVar) == alignof(cnd_t));

Mutex* asMutex(mtx_t* mutex) { return reinterpret_cast<Mutex*>(mutex); }
CndVar* asCondvar(cnd_t* cond) { return reinterpret_cast<CndVar*>(cond); }

int mutexError(MutexError error) {
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
  case MutexError::BAD_LOCK_STATE:
    return EINVAL;
  case MutexError::OVERFLOW:
    return EAGAIN;
  }
  __builtin_unreachable();
}
} // namespace

int __llvm_libc_mmix_cxx_mutex_init(mtx_t* mutex, bool recursive) {
  new (mutex) Mutex(false, recursive, false, false);
  return 0;
}
int __llvm_libc_mmix_cxx_mutex_lock(mtx_t* mutex) { return mutexError(asMutex(mutex)->lock()); }
bool __llvm_libc_mmix_cxx_mutex_trylock(mtx_t* mutex) { return asMutex(mutex)->try_lock() == MutexError::NONE; }
int __llvm_libc_mmix_cxx_mutex_unlock(mtx_t* mutex) { return mutexError(asMutex(mutex)->unlock()); }
int __llvm_libc_mmix_cxx_mutex_destroy(mtx_t* mutex) { return mutexError(Mutex::destroy(asMutex(mutex))); }
int __llvm_libc_mmix_cxx_condvar_signal(cnd_t* cond) {
  asCondvar(cond)->notify_one();
  return 0;
}
int __llvm_libc_mmix_cxx_condvar_broadcast(cnd_t* cond) {
  asCondvar(cond)->broadcast();
  return 0;
}
int __llvm_libc_mmix_cxx_condvar_wait(cnd_t* cond, mtx_t* mutex) {
  switch (asCondvar(cond)->wait(asMutex(mutex))) {
  case CndVarResult::Success:
    return 0;
  case CndVarResult::MutexError:
    return EINVAL;
  case CndVarResult::Timeout:
    return ETIMEDOUT;
  }
  __builtin_unreachable();
}
int __llvm_libc_mmix_cxx_condvar_destroy(cnd_t* cond) {
  asCondvar(cond)->reset();
  return 0;
}
pid_t __llvm_libc_mmix_cxx_current_id() { return LIBC_NAMESPACE::internal::gettid(); }
