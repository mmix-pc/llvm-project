//===-- MMIX Linux robust priority-protection mutex --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ROBUST_PROTECT_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ROBUST_PROTECT_MUTEX_H

#include "robust_mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
constexpr uint32_t MUTEX_ROBUST_PROTECT = mutex_flag(7);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr unsigned MUTEX_ROBUST_CEILING_SHIFT = 17;
#else
constexpr unsigned MUTEX_ROBUST_CEILING_SHIFT = 8;
#endif
constexpr uint32_t MUTEX_ROBUST_CEILING_MASK =
    127u << MUTEX_ROBUST_CEILING_SHIFT;
static_assert(!(MUTEX_ROBUST_PROTECT & MUTEX_ROBUST_CEILING_MASK));
static_assert(!((MUTEX_ROBUST_PROTECT | MUTEX_ROBUST_CEILING_MASK) &
                (MUTEX_INHERIT | MUTEX_RECURSIVE | MUTEX_ROBUST | MUTEX_SHARED |
                 MUTEX_ERRORCHECK | MUTEX_INCONSISTENT | MUTEX_UNRECOVERABLE)));

struct RobustProtectMutex : RobustMutex {
  constexpr RobustProtectMutex(int ceiling, bool recurse, bool shared,
                               bool check)
      : RobustMutex(recurse, shared, check, false, true,
                    MUTEX_ROBUST_PROTECT |
                        (uint32_t(ceiling) << MUTEX_ROBUST_CEILING_SHIFT)) {}
  int ceiling() const {
    return int((mutex_flags(this) & MUTEX_ROBUST_CEILING_MASK) >>
               MUTEX_ROBUST_CEILING_SHIFT);
  }
  bool owned_by_current();
  int acquire(cpp::optional<Futex::Timeout> timeout = cpp::nullopt,
              bool try_only = false, bool condition_recovery = false);
  int release();
  int set_ceiling(int value, int *previous);
};
static_assert(sizeof(RobustProtectMutex) == 24 &&
              alignof(RobustProtectMutex) == 8);
static_assert(__builtin_offsetof(RobustProtectMutex, futex) == 0);
static_assert(__builtin_offsetof(RobustProtectMutex, flags) == 4);
static_assert(__builtin_offsetof(RobustProtectMutex, link) == 8);
static_assert(__builtin_offsetof(RobustProtectMutex, lock_count) == 16);

inline bool uses_robust_protect_mutex(const void *storage) {
  uint32_t flags = mutex_flags(storage);
  return (flags & (MUTEX_ROBUST_PROTECT | MUTEX_ROBUST | MUTEX_INHERIT)) ==
         (MUTEX_ROBUST_PROTECT | MUTEX_ROBUST);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
