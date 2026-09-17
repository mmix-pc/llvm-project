//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBCXX_MMIX_LINUX_THREADING_H
#define LIBCXX_MMIX_LINUX_THREADING_H

#include <llvm-libc-types/cnd_t.h>
#include <llvm-libc-types/mtx_t.h>
#include <llvm-libc-types/pid_t.h>
#include <llvm-libc-types/tss_t.h>

struct timespec;

// Private bridge: libc's internal <new> and libc++ headers cannot share a TU.
extern "C" {
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_mutex_init(mtx_t* mutex, bool recursive);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_mutex_lock(mtx_t* mutex);
__attribute__((visibility("hidden"))) bool __llvm_libc_mmix_cxx_mutex_trylock(mtx_t* mutex);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_mutex_unlock(mtx_t* mutex);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_mutex_destroy(mtx_t* mutex);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_condvar_signal(cnd_t* cond);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_condvar_broadcast(cnd_t* cond);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_condvar_wait(cnd_t* cond, mtx_t* mutex);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_condvar_timedwait(cnd_t*, mtx_t*, const timespec*);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_condvar_destroy(cnd_t* cond);
__attribute__((visibility("hidden"))) pid_t __llvm_libc_mmix_cxx_current_id();
__attribute__((visibility("hidden"))) void __llvm_libc_mmix_cxx_sleep(const timespec*);
__attribute__((visibility("hidden"))) void __llvm_libc_mmix_cxx_yield();
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_key_create(tss_t*, void (*)(void*));
__attribute__((visibility("hidden"))) void* __llvm_libc_mmix_cxx_key_get(tss_t);
__attribute__((visibility("hidden"))) int __llvm_libc_mmix_cxx_key_set(tss_t, void*);
}

#endif
