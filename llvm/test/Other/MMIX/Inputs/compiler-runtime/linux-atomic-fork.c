// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "mmix/linux_atomic.c"

_Static_assert(SPINLOCK_COUNT >= 3, "Need distinct first, middle and last locks");

static int check_busy(size_t Busy, uintptr_t Value, bool MarkSuffix) {
  uintptr_t Before[SPINLOCK_COUNT];
  for (size_t I = 0; I != SPINLOCK_COUNT; ++I) {
    // Distinct occupied suffix entries expose accidental release or overwrite;
    // interleaved zero entries also verify that the suffix is not acquired.
    Before[I] = I == Busy ? Value : MarkSuffix && I > Busy && (I & 1) ? I + 2 : 0;
    __c11_atomic_store(&locks[I], Before[I], __ATOMIC_RELAXED);
  }
  for (unsigned Attempt = 0; Attempt != 2; ++Attempt) {
    if (__llvm_libc_mmix_atomic_try_prepare_fork())
      return 1;
    for (size_t I = 0; I != SPINLOCK_COUNT; ++I)
      if (__c11_atomic_load(&locks[I], __ATOMIC_RELAXED) != Before[I])
        return 2;
  }
  // A failed preparation must have rolled back without a finish call.
  for (size_t I = 0; I != SPINLOCK_COUNT; ++I)
    __c11_atomic_store(&locks[I], 0, __ATOMIC_RELAXED);
  return 0;
}

static int check_success(void) {
  if (!__llvm_libc_mmix_atomic_try_prepare_fork())
    return 3;
  for (size_t I = 0; I != SPINLOCK_COUNT; ++I)
    if (__c11_atomic_load(&locks[I], __ATOMIC_RELAXED) != 1)
      return 4;
  // A second preparation cannot steal or release the first one's locks.
  if (__llvm_libc_mmix_atomic_try_prepare_fork())
    return 5;
  for (size_t I = 0; I != SPINLOCK_COUNT; ++I)
    if (__c11_atomic_load(&locks[I], __ATOMIC_RELAXED) != 1)
      return 6;
  __llvm_libc_mmix_atomic_finish_fork();
  for (size_t I = 0; I != SPINLOCK_COUNT; ++I)
    if (__c11_atomic_load(&locks[I], __ATOMIC_RELAXED) != 0)
      return 7;
  return 0;
}

int main(void) {
  const size_t Busy[] = {0, SPINLOCK_COUNT / 2, SPINLOCK_COUNT - 1};
  int Result = check_success();
  if (Result)
    return Result;
  for (size_t I = 0; I != sizeof(Busy) / sizeof(Busy[0]); ++I) {
    for (unsigned Pattern = 0; Pattern != 2; ++Pattern) {
      Result = check_busy(Busy[I], Pattern ? 7 : 1, Pattern != 0);
      if (Result)
        return Result;
      Result = check_success();
      if (Result)
        return Result;
    }
  }
  return check_success();
}
