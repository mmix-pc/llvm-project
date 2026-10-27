//===-- In-place construction ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_CPP_CONSTRUCT_AT_H
#define LLVM_LIBC_SRC___SUPPORT_CPP_CONSTRUCT_AT_H

#include "src/__support/CPP/utility.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

#include <stddef.h>

// The private tag avoids defining standard placement new or importing libc's
// allocator declarations into shared headers used alongside the host <new>.
LIBC_INLINE void *operator new(size_t, LIBC_NAMESPACE::cpp::in_place_t,
                               void *address) noexcept {
  return address;
}

LIBC_INLINE void operator delete(void *, LIBC_NAMESPACE::cpp::in_place_t,
                                 void *) noexcept {}

namespace LIBC_NAMESPACE_DECL {
namespace cpp {

template <class T, class... Args>
LIBC_INLINE constexpr T *construct_at(T *address, Args &&...args) {
  return ::new (in_place, static_cast<void *>(address))
      T(forward<Args>(args)...);
}

} // namespace cpp
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_CPP_CONSTRUCT_AT_H
