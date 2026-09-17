//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Threading.h"
#include <__external_threading>
#include <cerrno>

_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS

int __libcpp_mutex_lock(__libcpp_mutex_t* mutex) { return __llvm_libc_mmix_cxx_mutex_lock(mutex); }
bool __libcpp_mutex_trylock(__libcpp_mutex_t* mutex) { return __llvm_libc_mmix_cxx_mutex_trylock(mutex); }
int __libcpp_mutex_unlock(__libcpp_mutex_t* mutex) { return __llvm_libc_mmix_cxx_mutex_unlock(mutex); }
int __libcpp_mutex_destroy(__libcpp_mutex_t* mutex) { return __llvm_libc_mmix_cxx_mutex_destroy(mutex); }
int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* mutex) {
  return __llvm_libc_mmix_cxx_mutex_init(mutex, true);
}
int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* mutex) { return __libcpp_mutex_lock(mutex); }
bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* mutex) { return __libcpp_mutex_trylock(mutex); }
int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* mutex) { return __libcpp_mutex_unlock(mutex); }
int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* mutex) { return __libcpp_mutex_destroy(mutex); }
int __libcpp_condvar_signal(__libcpp_condvar_t* cond) { return __llvm_libc_mmix_cxx_condvar_signal(cond); }
int __libcpp_condvar_broadcast(__libcpp_condvar_t* cond) { return __llvm_libc_mmix_cxx_condvar_broadcast(cond); }
int __libcpp_condvar_wait(__libcpp_condvar_t* cond, __libcpp_mutex_t* mutex) {
  return __llvm_libc_mmix_cxx_condvar_wait(cond, mutex);
}
int __libcpp_condvar_timedwait(__libcpp_condvar_t* cond, __libcpp_mutex_t* mutex, __libcpp_timespec_t* time) {
  return __llvm_libc_mmix_cxx_condvar_timedwait(cond, mutex, time);
}
int __libcpp_condvar_destroy(__libcpp_condvar_t* cond) { return __llvm_libc_mmix_cxx_condvar_destroy(cond); }
__libcpp_thread_id __libcpp_thread_get_current_id() { return __llvm_libc_mmix_cxx_current_id(); }
bool __libcpp_thread_id_equal(__libcpp_thread_id lhs, __libcpp_thread_id rhs) { return lhs == rhs; }
bool __libcpp_thread_id_less(__libcpp_thread_id lhs, __libcpp_thread_id rhs) { return lhs < rhs; }

int __libcpp_tls_create(__libcpp_tls_key* key, void (*dtor)(void*)) {
  return __llvm_libc_mmix_cxx_key_create(key, dtor);
}
void* __libcpp_tls_get(__libcpp_tls_key key) { return __llvm_libc_mmix_cxx_key_get(key); }
int __libcpp_tls_set(__libcpp_tls_key key, void* value) { return __llvm_libc_mmix_cxx_key_set(key, value); }
bool __libcpp_thread_isnull(const __libcpp_thread_t* thread) { return thread->__attrib == nullptr; }
// FIXME: Replace explicit failures with pthread services when threads/TLS are
// available. Whole thread-exit objects must not imply successful thread creation.
int __libcpp_thread_create(__libcpp_thread_t*, void* (*)(void*), void*) { return ENOSYS; }
int __libcpp_thread_join(__libcpp_thread_t*) { return ENOSYS; }
int __libcpp_thread_detach(__libcpp_thread_t*) { return ENOSYS; }
void __libcpp_thread_yield() { __llvm_libc_mmix_cxx_yield(); }
void __libcpp_thread_sleep_for(const chrono::nanoseconds& duration) {
  if (duration.count() <= 0)
    return;
  auto seconds = chrono::duration_cast<chrono::seconds>(duration);
  timespec time = {seconds.count(), (duration - seconds).count()};
  __llvm_libc_mmix_cxx_sleep(&time);
}

// Unused execute_once and foreign thread identity remain unavailable.
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD
