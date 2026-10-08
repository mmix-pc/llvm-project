//===-- MMIX Linux mutex flag storage ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_MUTEX_FLAGS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_MUTEX_FLAGS_H
#include "src/__support/macros/config.h"
#include <stdint.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
constexpr uint32_t mutex_flag(unsigned index) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return uint32_t(1) << (31 - index);
#else
  return uint32_t(1) << index;
#endif
}
constexpr uint32_t MUTEX_INHERIT = mutex_flag(0);
constexpr uint32_t MUTEX_RECURSIVE = mutex_flag(1);
constexpr uint32_t MUTEX_ROBUST = mutex_flag(2);
constexpr uint32_t MUTEX_SHARED = mutex_flag(3);
constexpr uint32_t MUTEX_ERRORCHECK = mutex_flag(4);
constexpr uint32_t MUTEX_INCONSISTENT = mutex_flag(5);
constexpr uint32_t MUTEX_UNRECOVERABLE = mutex_flag(6);

inline uint32_t mutex_flags(const void *storage) {
  // All admitted representations place the flag word after the u32 futex.
  // Recovery updates and dispatch must access the entire word atomically.
  auto *word = reinterpret_cast<const uint32_t *>(
      static_cast<const unsigned char *>(storage) + sizeof(uint32_t));
  return __atomic_load_n(word, __ATOMIC_RELAXED);
}
inline bool uses_kernel_mutex(const void *storage) {
  return mutex_flags(storage) & (MUTEX_INHERIT | MUTEX_ROBUST);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
