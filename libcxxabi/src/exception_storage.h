//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBCXXABI_EXCEPTION_STORAGE_H
#define LIBCXXABI_EXCEPTION_STORAGE_H

#include "fallback_malloc.h"
#if defined(_LIBCXXABI_MMIX_EXCEPTION_OWNERS)
#  include "mmix/ExceptionStorage.h"
#endif

namespace __cxxabiv1 {
inline void* allocate_exception_storage(size_t size) {
#if defined(_LIBCXXABI_MMIX_EXCEPTION_OWNERS)
  return mmix::allocate_exception_storage(size);
#else
  return __aligned_malloc_with_fallback(size);
#endif
}

inline void free_exception_storage(void* storage) {
#if defined(_LIBCXXABI_MMIX_EXCEPTION_OWNERS)
  mmix::free_exception_storage(storage);
#else
  __aligned_free_with_fallback(storage);
#endif
}
} // namespace __cxxabiv1
#endif
