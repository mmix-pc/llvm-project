//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBCXXABI_MMIX_EXCEPTION_STORAGE_H
#define LIBCXXABI_MMIX_EXCEPTION_STORAGE_H

#include "../fallback_malloc.h"
#include <stddef.h>
#include <stdint.h>

namespace __cxxabiv1 {
namespace mmix {
_LIBCXXABI_HIDDEN void* allocate_exception_storage(size_t size);
_LIBCXXABI_HIDDEN void free_exception_storage(void* storage);
_LIBCXXABI_HIDDEN void retain_exception_storage(
    void* storage, uintptr_t type, uintptr_t destructor, uintptr_t unexpected, uintptr_t terminate);
} // namespace mmix
} // namespace __cxxabiv1
#endif
