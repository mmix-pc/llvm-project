// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DWIDE -Wno-atomic-alignment -c %s -o %t.wide.o
// RUN: llvm-nm --undefined-only %t.wide.o \
// RUN:   | FileCheck %s --check-prefix=WIDE
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DUNALIGNED -Wno-address-of-packed-member -c %s \
// RUN:   -o %t.unaligned.o
// RUN: llvm-nm --undefined-only %t.unaligned.o \
// RUN:   | FileCheck %s --check-prefix=UNALIGNED
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DADDRESS_SPACE -c %s -o %t.address-space.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADDRESS-SPACE
// RUN: not test -s %t.address-space.o
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DRUNTIME_QUERY -c %s -o %t.runtime-query.o
// RUN: llvm-nm --undefined-only %t.runtime-query.o \
// RUN:   | FileCheck %s --check-prefix=RUNTIME-QUERY
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DWIDE_SYNC -c %s -o %t.wide-sync.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WIDE-SYNC
// RUN: not test -s %t.wide-sync.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DINVALID_ORDER -Werror=atomic-memory-ordering -c %s \
// RUN:   -o %t.invalid-order.o 2>&1 | FileCheck %s --check-prefix=ORDER
// RUN: not test -s %t.invalid-order.o
// RUN: not %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 \
// RUN:   -DSCOPED_NAND -c %s -o %t.scoped-nand.o 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SCOPED-NAND
// RUN: not test -s %t.scoped-nand.o

// WIDE: U __atomic_load
// UNALIGNED-DAG: U __atomic_compare_exchange
// UNALIGNED-DAG: U __atomic_load
// ADDRESS-SPACE: error: MMIX GNU ABI does not support argument type 'address_space_one *'
// RUNTIME-QUERY: U __atomic_is_lock_free
// WIDE-SYNC: error: MMIX GNU ABI does not support atomic builtin __sync_fetch_and_add_16
// ORDER: error: memory order argument to atomic operation is invalid
// SCOPED-NAND: error: MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_nand

#if defined(WIDE)
struct ThreeBytes {
  unsigned char bytes[3];
};
void wide_load(struct ThreeBytes *ptr, struct ThreeBytes *result) {
  __atomic_load(ptr, result, __ATOMIC_RELAXED);
}
#elif defined(UNALIGNED)
struct __attribute__((packed)) Packed {
  unsigned char padding;
  unsigned value;
};
unsigned unaligned_add(struct Packed *ptr) {
  return __atomic_fetch_add(&ptr->value, 1, __ATOMIC_RELAXED);
}
#elif defined(ADDRESS_SPACE)
typedef unsigned address_space_one __attribute__((address_space(1)));
unsigned address_space_load(address_space_one *ptr) {
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}
#elif defined(RUNTIME_QUERY)
int runtime_query(unsigned long size, void *ptr) {
  return __atomic_is_lock_free(size, ptr);
}
#elif defined(WIDE_SYNC)
__int128 wide_sync(__int128 *ptr) { return __sync_fetch_and_add(ptr, 1); }
#elif defined(INVALID_ORDER)
unsigned invalid_load_order(unsigned *ptr) {
  return __atomic_load_n(ptr, __ATOMIC_RELEASE);
}
#elif defined(SCOPED_NAND)
unsigned scoped_nand(unsigned *ptr) {
  return __scoped_atomic_fetch_nand(ptr, 1, __ATOMIC_RELAXED,
                                    __MEMORY_SCOPE_SYSTEM);
}
#endif
