// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DFLOAT_RMW -c %s -o %t.float.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=FLOAT
// RUN: not test -s %t.float.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DMIN -c %s -o %t.min.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MIN
// RUN: not test -s %t.min.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DMAX -c %s -o %t.max.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MAX
// RUN: not test -s %t.max.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DNAND -c %s -o %t.nand.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NAND
// RUN: not test -s %t.nand.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DUINC -c %s -o %t.uinc.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UINC
// RUN: not test -s %t.uinc.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DUDEC -c %s -o %t.udec.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UDEC
// RUN: not test -s %t.udec.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DWIDE -c %s -o %t.wide.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WIDE
// RUN: not test -s %t.wide.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DADDRESS_SPACE -c %s -o %t.address-space.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADDRESS-SPACE
// RUN: not test -s %t.address-space.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DINVALID_LOAD_ORDER -Werror=atomic-memory-ordering \
// RUN:   -c %s -o %t.load-order.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ORDER
// RUN: not test -s %t.load-order.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DINVALID_STORE_ORDER -Werror=atomic-memory-ordering \
// RUN:   -c %s -o %t.store-order.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ORDER
// RUN: not test -s %t.store-order.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DINVALID_FAILURE_ORDER -Werror=atomic-memory-ordering \
// RUN:   -c %s -o %t.failure-order.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=FAILURE-ORDER
// RUN: not test -s %t.failure-order.o

// FLOAT: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_add
// MIN: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_min
// MAX: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_max
// NAND: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_nand
// UINC: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_uinc
// UDEC: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_udec
// WIDE: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_add
// ADDRESS-SPACE: error: MMIX GNU ABI does not support argument type 'address_space_one *'
// ORDER: error: memory order argument to atomic operation is invalid
// FAILURE-ORDER: error: failure memory order argument to atomic operation is invalid

#if defined(FLOAT_RMW)
float unsupported_float(float *ptr, float value) {
  return __scoped_atomic_fetch_add(ptr, value, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_SYSTEM);
}
#elif defined(MIN)
unsigned unsupported_min(unsigned *ptr, unsigned value) {
  return __scoped_atomic_fetch_min(ptr, value, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_SYSTEM);
}
#elif defined(MAX)
unsigned unsupported_max(unsigned *ptr, unsigned value) {
  return __scoped_atomic_fetch_max(ptr, value, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_SYSTEM);
}
#elif defined(NAND)
unsigned unsupported_nand(unsigned *ptr, unsigned value) {
  return __scoped_atomic_fetch_nand(ptr, value, __ATOMIC_RELAXED,
                                    __MEMORY_SCOPE_SYSTEM);
}
#elif defined(UINC)
unsigned unsupported_uinc(unsigned *ptr, unsigned value) {
  return __scoped_atomic_fetch_uinc(ptr, value, __ATOMIC_RELAXED,
                                    __MEMORY_SCOPE_SYSTEM);
}
#elif defined(UDEC)
unsigned unsupported_udec(unsigned *ptr, unsigned value) {
  return __scoped_atomic_fetch_udec(ptr, value, __ATOMIC_RELAXED,
                                    __MEMORY_SCOPE_SYSTEM);
}
#elif defined(WIDE)
__int128 unsupported_wide(__int128 *ptr, __int128 value) {
  return __scoped_atomic_fetch_add(ptr, value, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_SYSTEM);
}
#elif defined(ADDRESS_SPACE)
typedef unsigned address_space_one __attribute__((address_space(1)));
unsigned unsupported_address_space(address_space_one *ptr) {
  return __scoped_atomic_fetch_add(ptr, 1, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_SYSTEM);
}
#elif defined(INVALID_LOAD_ORDER)
unsigned invalid_load_order(unsigned *ptr) {
  return __scoped_atomic_load_n(ptr, __ATOMIC_RELEASE,
                                __MEMORY_SCOPE_SYSTEM);
}
#elif defined(INVALID_STORE_ORDER)
void invalid_store_order(unsigned *ptr, unsigned value) {
  __scoped_atomic_store_n(ptr, value, __ATOMIC_ACQUIRE,
                          __MEMORY_SCOPE_SYSTEM);
}
#elif defined(INVALID_FAILURE_ORDER)
_Bool invalid_failure_order(unsigned *ptr, unsigned *expected,
                            unsigned desired) {
  return __scoped_atomic_compare_exchange_n(
      ptr, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELEASE,
      __MEMORY_SCOPE_SYSTEM);
}
#endif
