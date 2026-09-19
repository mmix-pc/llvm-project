// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -Wno-sync-fetch-and-nand-semantics-changed \
// RUN:   -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -Wno-sync-fetch-and-nand-semantics-changed \
// RUN:   -S %s -o - | FileCheck %s --check-prefix=ASM \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_

// IR-LABEL: define {{.*}} i1 @atomic_test_and_set(
// IR: atomicrmw xchg {{.*}} i8 1 acquire, align 1
// IR-LABEL: define {{.*}} void @atomic_clear(
// IR: store atomic i8 0, {{.*}} release, align 1
// IR-LABEL: define {{.*}} i32 @native_always_lock_free(
// IR: ret i32 1

// Legacy fetch-and-operation builtins return the old value and retain their
// full sequentially consistent ordering.
// IR-LABEL: define {{.*}} i8 @sync_fetch_add_i8(
// IR: atomicrmw add {{.*}} i8 {{.*}} seq_cst, align 1
// IR-LABEL: define {{.*}} i16 @sync_fetch_sub_i16(
// IR: atomicrmw sub {{.*}} i16 {{.*}} seq_cst, align 2
// IR-LABEL: define {{.*}} i32 @sync_fetch_or_i32(
// IR: atomicrmw or {{.*}} i32 {{.*}} seq_cst, align 4
// IR-LABEL: define {{.*}} i64 @sync_fetch_and_i64(
// IR: atomicrmw and {{.*}} i64 {{.*}} seq_cst, align 8
// IR-LABEL: define {{.*}} i32 @sync_fetch_xor_i32(
// IR: atomicrmw xor {{.*}} i32 {{.*}} seq_cst, align 4
// IR-LABEL: define {{.*}} i16 @sync_fetch_nand_i16(
// IR: atomicrmw nand {{.*}} i16 {{.*}} seq_cst, align 2

// Legacy operation-and-fetch builtins explicitly return the computed value.
// IR-LABEL: define {{.*}} i64 @sync_add_fetch(
// IR: [[ADD_OLD:%.*]] = atomicrmw add {{.*}} seq_cst, align 8
// IR: [[ADD_NEW:%.*]] = add i64 [[ADD_OLD]],
// IR: ret i64 [[ADD_NEW]]
// IR-LABEL: define {{.*}} i64 @sync_sub_fetch(
// IR: atomicrmw sub {{.*}} seq_cst, align 8
// IR: sub i64
// IR-LABEL: define {{.*}} i64 @sync_or_fetch(
// IR: atomicrmw or {{.*}} seq_cst, align 8
// IR: or i64
// IR-LABEL: define {{.*}} i64 @sync_and_fetch(
// IR: atomicrmw and {{.*}} seq_cst, align 8
// IR: and i64
// IR-LABEL: define {{.*}} i64 @sync_xor_fetch(
// IR: atomicrmw xor {{.*}} seq_cst, align 8
// IR: xor i64
// IR-LABEL: define {{.*}} i64 @sync_nand_fetch(
// IR: atomicrmw nand {{.*}} seq_cst, align 8
// IR: xor i64 {{.*}}, -1

// IR-LABEL: define {{.*}} i32 @sync_val_compare_swap(
// IR: [[VAL_PAIR:%.*]] = cmpxchg {{.*}} i32 {{.*}} seq_cst seq_cst, align 4
// IR: [[VAL_OLD:%.*]] = extractvalue { i32, i1 } [[VAL_PAIR]], 0
// IR: ret i32 [[VAL_OLD]]
// IR-LABEL: define {{.*}} i1 @sync_bool_compare_swap(
// IR: [[BOOL_PAIR:%.*]] = cmpxchg {{.*}} i64 {{.*}} seq_cst seq_cst, align 8
// IR: [[BOOL_OK:%.*]] = extractvalue { i64, i1 } [[BOOL_PAIR]], 1
// IR: ret i1 [[BOOL_OK]]
// IR-LABEL: define {{.*}} i8 @sync_lock_test_and_set(
// IR: atomicrmw xchg {{.*}} i8 1 seq_cst, align 1
// IR-LABEL: define {{.*}} void @sync_lock_release(
// IR: store atomic i8 0, {{.*}} release, align 1
// IR-LABEL: define {{.*}} void @sync_synchronize(
// IR: fence seq_cst

// ASM-LABEL: atomic_test_and_set:
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: atomic_clear:
// ASM: SYNC 3
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: native_always_lock_free:
// ASM: SETL r231, 1

// Four widths exercise the same native loop without runtime calls.
// ASM-LABEL: sync_fetch_add_i8:
// ASM: SYNC 3
// ASM: LDOU
// ASM: ADDU
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: sync_fetch_sub_i16:
// ASM: LDOU
// ASM: SUBU
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_fetch_or_i32:
// ASM: LDOU
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_fetch_and_i64:
// ASM: LDOU
// ASM: AND
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_fetch_xor_i32:
// ASM: LDOU
// ASM: XOR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_fetch_nand_i16:
// ASM: LDOU
// ASM: ANDN
// ASM: CSWAP
// ASM: BNZB

// ASM-LABEL: sync_add_fetch:
// ASM: ADDU
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_sub_fetch:
// ASM: SUBU
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_or_fetch:
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_and_fetch:
// ASM: AND
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_xor_fetch:
// ASM: XOR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_nand_fetch:
// ASM: NAND
// ASM: CSWAP
// ASM: BNZB

// ASM-LABEL: sync_val_compare_swap:
// ASM: CSWAP
// ASM: SYNC 3
// ASM-LABEL: sync_bool_compare_swap:
// ASM: CSWAP
// ASM: SYNC 3
// ASM-LABEL: sync_lock_test_and_set:
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_lock_release:
// ASM: SYNC 3
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: sync_synchronize:
// ASM: SYNC 3

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

_Bool atomic_test_and_set(u8 *ptr) {
  return __atomic_test_and_set(ptr, __ATOMIC_ACQUIRE);
}

void atomic_clear(u8 *ptr) { __atomic_clear(ptr, __ATOMIC_RELEASE); }

int native_always_lock_free(void) {
  return __atomic_always_lock_free(8, 0);
}

u8 sync_fetch_add_i8(u8 *ptr, u8 value) {
  return __sync_fetch_and_add(ptr, value);
}

u16 sync_fetch_sub_i16(u16 *ptr, u16 value) {
  return __sync_fetch_and_sub(ptr, value);
}

u32 sync_fetch_or_i32(u32 *ptr, u32 value) {
  return __sync_fetch_and_or(ptr, value);
}

u64 sync_fetch_and_i64(u64 *ptr, u64 value) {
  return __sync_fetch_and_and(ptr, value);
}

u32 sync_fetch_xor_i32(u32 *ptr, u32 value) {
  return __sync_fetch_and_xor(ptr, value);
}

u16 sync_fetch_nand_i16(u16 *ptr, u16 value) {
  return __sync_fetch_and_nand(ptr, value);
}

u64 sync_add_fetch(u64 *ptr, u64 value) {
  return __sync_add_and_fetch(ptr, value);
}

u64 sync_sub_fetch(u64 *ptr, u64 value) {
  return __sync_sub_and_fetch(ptr, value);
}

u64 sync_or_fetch(u64 *ptr, u64 value) {
  return __sync_or_and_fetch(ptr, value);
}

u64 sync_and_fetch(u64 *ptr, u64 value) {
  return __sync_and_and_fetch(ptr, value);
}

u64 sync_xor_fetch(u64 *ptr, u64 value) {
  return __sync_xor_and_fetch(ptr, value);
}

u64 sync_nand_fetch(u64 *ptr, u64 value) {
  return __sync_nand_and_fetch(ptr, value);
}

u32 sync_val_compare_swap(u32 *ptr, u32 expected, u32 desired) {
  return __sync_val_compare_and_swap(ptr, expected, desired);
}

_Bool sync_bool_compare_swap(u64 *ptr, u64 expected, u64 desired) {
  return __sync_bool_compare_and_swap(ptr, expected, desired);
}

u8 sync_lock_test_and_set(u8 *ptr) {
  return __sync_lock_test_and_set(ptr, 1);
}

void sync_lock_release(u8 *ptr) { __sync_lock_release(ptr); }

void sync_synchronize(void) { __sync_synchronize(); }
