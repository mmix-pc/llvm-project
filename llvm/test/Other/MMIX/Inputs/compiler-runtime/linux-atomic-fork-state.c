// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "mmix/linux_atomic.c"

size_t test_atomic_lock_count(void) { return SPINLOCK_COUNT; }
void test_atomic_lock_set(size_t index, uintptr_t value) {
  __c11_atomic_store(&locks[index], value, __ATOMIC_RELAXED);
}
uintptr_t test_atomic_lock_get(size_t index) {
  return __c11_atomic_load(&locks[index], __ATOMIC_RELAXED);
}
