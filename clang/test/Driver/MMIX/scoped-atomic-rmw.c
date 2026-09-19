// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not='syncscope('
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=ASM --implicit-check-not=__atomic_
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -c %s -o %t.o
// RUN: llvm-readobj --file-headers --symbols --relocations %t.o \
// RUN:   | FileCheck %s --check-prefix=ELF --implicit-check-not=__atomic_
// RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
// RUN:   | FileCheck %s --check-prefix=OBJ --implicit-check-not='<unknown>' \
// RUN:   --implicit-check-not=__atomic_

// IR-LABEL: define{{.*}} i8 @fetch_add_i8(
// IR: atomicrmw add ptr %{{.*}}, i8 %{{.*}} monotonic, align 1
// IR-LABEL: define{{.*}} i16 @fetch_sub_i16(
// IR: atomicrmw sub ptr %{{.*}}, i16 %{{.*}} acquire, align 2
// IR-LABEL: define{{.*}} i32 @fetch_and_i32(
// IR: atomicrmw and ptr %{{.*}}, i32 %{{.*}} release, align 4
// IR-LABEL: define{{.*}} i64 @fetch_or_i64(
// IR: atomicrmw or ptr %{{.*}}, i64 %{{.*}} acq_rel, align 8
// IR-LABEL: define{{.*}} i8 @fetch_xor_i8(
// IR: atomicrmw xor ptr %{{.*}}, i8 %{{.*}} seq_cst, align 1
// IR-LABEL: define{{.*}} i64 @fetch_add_runtime_scope(
// IR: atomicrmw add ptr %{{.*}}, i64 %{{.*}} acquire, align 8

// IR-LABEL: define{{.*}} i1 @compare_strong_i8(
// IR: [[STRONG8:%.*]] = cmpxchg ptr %{{.*}}, i8 %{{.*}} acq_rel acquire, align 1
// IR: [[STRONG8_OK:%.*]] = extractvalue { i8, i1 } [[STRONG8]], 1
// IR: br i1 [[STRONG8_OK]],
// IR: [[STRONG8_VALUE:%.*]] = extractvalue { i8, i1 } [[STRONG8]], 0
// IR: store i8 [[STRONG8_VALUE]], ptr %{{.*}}, align 1
// IR: ret i1 [[STRONG8_OK]]
// IR-LABEL: define{{.*}} i1 @compare_weak_i16(
// IR: [[WEAK16:%.*]] = cmpxchg weak ptr %{{.*}}, i16 %{{.*}} acquire monotonic, align 2
// IR: [[WEAK16_OK:%.*]] = extractvalue { i16, i1 } [[WEAK16]], 1
// IR: br i1 [[WEAK16_OK]],
// IR: [[WEAK16_VALUE:%.*]] = extractvalue { i16, i1 } [[WEAK16]], 0
// IR: store i16 [[WEAK16_VALUE]], ptr %{{.*}}, align 2
// IR: ret i1 [[WEAK16_OK]]
// IR-LABEL: define{{.*}} i1 @compare_weak_i32(
// IR: cmpxchg weak ptr %{{.*}}, i32 %{{.*}} release monotonic, align 4
// IR-LABEL: define{{.*}} i1 @compare_generic_i64(
// IR: cmpxchg ptr %{{.*}}, i64 %{{.*}} seq_cst seq_cst, align 8

// ASM-LABEL: fetch_add_i8:
// ASM: LDOU
// ASM: ADDU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: fetch_sub_i16:
// ASM: LDOU
// ASM: SUBU
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: fetch_and_i32:
// ASM: SYNC 3
// ASM: LDOU
// ASM: AND
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: fetch_or_i64:
// ASM: SYNC 3
// ASM: LDOU
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: fetch_xor_i8:
// ASM: SYNC 3
// ASM: LDOU
// ASM: XOR
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: compare_strong_i8:
// ASM: CSWAP
// ASM-LABEL: compare_weak_i16:
// ASM: CSWAP
// ASM-LABEL: compare_weak_i32:
// ASM: CSWAP
// ASM-LABEL: compare_generic_i64:
// ASM: CSWAP

// ELF: Format: elf64-mmix
// ELF: Type: Relocatable
// ELF: Relocations [
// ELF-NEXT: ]

// OBJ-LABEL: <fetch_add_i8>:
// OBJ: LDOU
// OBJ: ADDU
// OBJ: AND
// OBJ: OR
// OBJ: CSWAP
// OBJ: BNZB
// OBJ-LABEL: <fetch_or_i64>:
// OBJ: SYNC 3
// OBJ: LDOU
// OBJ: OR
// OBJ: CSWAP
// OBJ: BNZB
// OBJ: SYNC 3
// OBJ-LABEL: <compare_strong_i8>:
// OBJ: CSWAP
// OBJ-LABEL: <compare_generic_i64>:
// OBJ: SYNC 3
// OBJ: CSWAP
// OBJ: SYNC 3

typedef unsigned char u8;
typedef signed short i16;
typedef unsigned int u32;
typedef signed long i64;
typedef unsigned long u64;

u8 fetch_add_i8(u8 *ptr, u8 value) {
  return __scoped_atomic_fetch_add(ptr, value, __ATOMIC_RELAXED,
                                   __MEMORY_SCOPE_SYSTEM);
}

i16 fetch_sub_i16(i16 *ptr, i16 value) {
  return __scoped_atomic_fetch_sub(ptr, value, __ATOMIC_ACQUIRE,
                                   __MEMORY_SCOPE_DEVICE);
}

u32 fetch_and_i32(u32 *ptr, u32 value) {
  return __scoped_atomic_fetch_and(ptr, value, __ATOMIC_RELEASE,
                                   __MEMORY_SCOPE_WRKGRP);
}

i64 fetch_or_i64(i64 *ptr, i64 value) {
  return __scoped_atomic_fetch_or(ptr, value, __ATOMIC_ACQ_REL,
                                  __MEMORY_SCOPE_WVFRNT);
}

u8 fetch_xor_i8(u8 *ptr, u8 value) {
  return __scoped_atomic_fetch_xor(ptr, value, __ATOMIC_SEQ_CST,
                                   __MEMORY_SCOPE_SINGLE);
}

u64 fetch_add_runtime_scope(u64 *ptr, u64 value, int scope) {
  return __scoped_atomic_fetch_add(ptr, value, __ATOMIC_CONSUME, scope);
}

_Bool compare_strong_i8(u8 *ptr, u8 *expected, u8 desired) {
  return __scoped_atomic_compare_exchange_n(
      ptr, expected, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE,
      __MEMORY_SCOPE_DEVICE);
}

_Bool compare_weak_i16(i16 *ptr, i16 *expected, i16 desired) {
  return __scoped_atomic_compare_exchange_n(
      ptr, expected, desired, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED,
      __MEMORY_SCOPE_WRKGRP);
}

_Bool compare_weak_i32(u32 *ptr, u32 *expected, u32 desired) {
  return __scoped_atomic_compare_exchange_n(
      ptr, expected, desired, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED,
      __MEMORY_SCOPE_CLUSTR);
}

_Bool compare_generic_i64(i64 *ptr, i64 *expected, i64 *desired, int scope) {
  return __scoped_atomic_compare_exchange(
      ptr, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST, scope);
}
