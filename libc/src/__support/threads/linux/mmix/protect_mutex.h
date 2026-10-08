//===-- MMIX Linux priority-protection mutex ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_PROTECT_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_PROTECT_MUTEX_H
#include "mutex_flags.h"
#include "hdr/types/pid_t.h"
#include "src/__support/threads/raw_mutex.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Recovery bits have no meaning in a stalled, non-PI object. Bit 5 selects
// protection there; the following seven bits hold its absolute ceiling.
constexpr uint32_t MUTEX_PROTECT = mutex_flag(5);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr unsigned MUTEX_CEILING_SHIFT = 19;
#else
constexpr unsigned MUTEX_CEILING_SHIFT = 6;
#endif
constexpr uint32_t MUTEX_CEILING_MASK = 127u << MUTEX_CEILING_SHIFT;
inline bool uses_protect_mutex(const void *storage) {
  uint32_t flags = mutex_flags(storage);
  return (flags & MUTEX_PROTECT) && !(flags & (MUTEX_ROBUST | MUTEX_INHERIT));
}
struct ProtectMutex : RawMutex {
  cpp::Atomic<uint32_t> flags;
  cpp::Atomic<pid_t> owner;
  size_t lock_count;
  constexpr ProtectMutex(int ceiling, bool recurse, bool shared, bool check)
      : flags(MUTEX_PROTECT | (uint32_t(ceiling) << MUTEX_CEILING_SHIFT) |
              (recurse ? MUTEX_RECURSIVE : 0) | (shared ? MUTEX_SHARED : 0) |
              (check ? MUTEX_ERRORCHECK : 0)),
        owner(0), lock_count(0) {}
  int ceiling() const {
    return int((mutex_flags(this) & MUTEX_CEILING_MASK) >> MUTEX_CEILING_SHIFT);
  }
  bool has_flag(uint32_t mask) const { return mutex_flags(this) & mask; }
  int acquire(cpp::optional<Futex::Timeout> timeout = cpp::nullopt,
              bool try_only = false);
  int release();
  int set_ceiling(int value, int *previous);
  int destroy();
};
static_assert(sizeof(ProtectMutex) == 24 && alignof(ProtectMutex) == 8);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
