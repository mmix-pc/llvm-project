//===-- linux_atomic.c - MMIX Linux atomic fork adaptation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "atomic.c"

// The canonical libc provider shares this table with every fallback atomic.
// Try each lock once: a busy lock must not make fork wait for another thread.
__attribute__((visibility("hidden"))) bool
__llvm_libc_mmix_atomic_try_prepare_fork(void) {
  for (size_t I = 0; I != SPINLOCK_COUNT; ++I) {
    uintptr_t Expected = 0;
    if (!__c11_atomic_compare_exchange_strong(
            &locks[I], &Expected, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      while (I != 0)
        unlock(&locks[--I]);
      return false;
    }
  }
  return true;
}

// Call only after successful preparation, in the parent (including fork
// failure) and child, before callbacks or any fallback atomic operation.
// This adds no signal, asynchronous-cancellation, or process-shared guarantee.
__attribute__((visibility("hidden"))) void
__llvm_libc_mmix_atomic_finish_fork(void) {
  for (size_t I = SPINLOCK_COUNT; I != 0;)
    unlock(&locks[--I]);
}
