//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cxxabi.h"

#if !defined(__mmix__) || !defined(__linux__) || \
    !defined(HAVE___CXA_THREAD_ATEXIT_IMPL) || defined(_LIBCXXABI_HAS_NO_THREADS)
#error "MMIX TLS registration requires the native Linux libc thread hook"
#endif

namespace __cxxabiv1 {
extern "C" int __cxa_thread_atexit_impl(void (*)(void*), void*, void*);

extern "C" _LIBCXXABI_FUNC_VIS int
__cxa_thread_atexit(void (*dtor)(void*), void* object, void* dso) throw() {
  // Compiler-generated callers can ignore a failure return. Do not let them
  // publish initialization without destruction; avoid allocation or formatting
  // when registration has already failed, possibly from exhausted memory.
  if (__cxa_thread_atexit_impl(dtor, object, dso) != 0)
    __builtin_abort();
  return 0;
}
} // namespace __cxxabiv1
