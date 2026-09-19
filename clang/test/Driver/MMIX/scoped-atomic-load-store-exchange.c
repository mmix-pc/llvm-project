// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -S -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not='syncscope('
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -S %s -o - \
// RUN:   | FileCheck %s --check-prefix=ASM --implicit-check-not=__atomic_

// IR-LABEL: define{{.*}} i8 @load_relaxed_i8(
// IR: load atomic i8, ptr %{{.*}} monotonic, align 1
// IR-LABEL: define{{.*}} i16 @load_consume_i16(
// IR: load atomic i16, ptr %{{.*}} acquire, align 2
// IR-LABEL: define{{.*}} i32 @load_acquire_i32(
// IR: load atomic i32, ptr %{{.*}} acquire, align 4
// IR-LABEL: define{{.*}} i64 @load_seq_cst_i64(
// IR: load atomic i64, ptr %{{.*}} seq_cst, align 8
// IR-LABEL: define{{.*}} void @load_generic_runtime_scope(
// IR: load atomic i64, ptr %{{.*}} acquire, align 8

// IR-LABEL: define{{.*}} void @store_relaxed_i8(
// IR: store atomic i8 %{{.*}}, ptr %{{.*}} monotonic, align 1
// IR-LABEL: define{{.*}} void @store_release_i16(
// IR: store atomic i16 %{{.*}}, ptr %{{.*}} release, align 2
// IR-LABEL: define{{.*}} void @store_seq_cst_i32(
// IR: store atomic i32 %{{.*}}, ptr %{{.*}} seq_cst, align 4
// IR-LABEL: define{{.*}} void @store_release_i64(
// IR: store atomic i64 %{{.*}}, ptr %{{.*}} release, align 8
// IR-LABEL: define{{.*}} void @store_generic_runtime_scope(
// IR: store atomic i64 %{{.*}}, ptr %{{.*}} release, align 8

// IR-LABEL: define{{.*}} i8 @exchange_relaxed_i8(
// IR: atomicrmw xchg ptr %{{.*}}, i8 %{{.*}} monotonic, align 1
// IR-LABEL: define{{.*}} i16 @exchange_consume_i16(
// IR: atomicrmw xchg ptr %{{.*}}, i16 %{{.*}} acquire, align 2
// IR-LABEL: define{{.*}} i32 @exchange_acquire_i32(
// IR: atomicrmw xchg ptr %{{.*}}, i32 %{{.*}} acquire, align 4
// IR-LABEL: define{{.*}} i32 @exchange_release_i32(
// IR: atomicrmw xchg ptr %{{.*}}, i32 %{{.*}} release, align 4
// IR-LABEL: define{{.*}} i64 @exchange_acq_rel_i64(
// IR: atomicrmw xchg ptr %{{.*}}, i64 %{{.*}} acq_rel, align 8
// IR-LABEL: define{{.*}} i8 @exchange_seq_cst_i8(
// IR: atomicrmw xchg ptr %{{.*}}, i8 %{{.*}} seq_cst, align 1
// IR-LABEL: define{{.*}} void @exchange_generic_runtime_scope(
// IR: atomicrmw xchg ptr %{{.*}}, i64 %{{.*}} seq_cst, align 8

// IR-LABEL: define{{.*}} void @fence_relaxed(
// IR-NOT: fence
// IR: ret void
// IR-LABEL: define{{.*}} void @fence_acquire(
// IR: fence acquire
// IR-LABEL: define{{.*}} void @fence_consume(
// IR: fence acquire
// IR-LABEL: define{{.*}} void @fence_release(
// IR: fence release
// IR-LABEL: define{{.*}} void @fence_acq_rel(
// IR: fence acq_rel
// IR-LABEL: define{{.*}} void @fence_seq_cst(
// IR: fence seq_cst

// ASM-LABEL: load_relaxed_i8:
// ASM: LDBU r231, r231, 0
// ASM-LABEL: load_consume_i16:
// ASM: LDW{{U?}} r231, r231, 0
// ASM: SYNC 3
// ASM-LABEL: load_acquire_i32:
// ASM: LDTU r231, r231, 0
// ASM: SYNC 3
// ASM-LABEL: load_seq_cst_i64:
// ASM: SYNC 3
// ASM: LDOU r231, r231, 0
// ASM: SYNC 3
// ASM-LABEL: store_relaxed_i8:
// ASM: CSWAP
// ASM-LABEL: store_release_i16:
// ASM: SYNC 3
// ASM: CSWAP
// ASM-LABEL: exchange_acquire_i32:
// ASM: CSWAP
// ASM: SYNC 3
// ASM-LABEL: exchange_release_i32:
// ASM: SYNC 3
// ASM: CSWAP
// ASM-LABEL: exchange_acq_rel_i64:
// ASM: SYNC 3
// ASM: CSWAP
// ASM: SYNC 3
// ASM-LABEL: fence_relaxed:
// ASM-NOT: SYNC
// ASM: POP 0, 0
// ASM-LABEL: fence_acquire:
// ASM: SYNC 3
// ASM-LABEL: fence_consume:
// ASM: SYNC 3
// ASM-LABEL: fence_release:
// ASM: SYNC 3
// ASM-LABEL: fence_acq_rel:
// ASM: SYNC 3
// ASM-LABEL: fence_seq_cst:
// ASM: SYNC 3

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

u8 load_relaxed_i8(u8 *ptr) {
  return __scoped_atomic_load_n(ptr, __ATOMIC_RELAXED,
                                __MEMORY_SCOPE_DEVICE);
}

u16 load_consume_i16(u16 *ptr) {
  return __scoped_atomic_load_n(ptr, __ATOMIC_CONSUME,
                                __MEMORY_SCOPE_WRKGRP);
}

u32 load_acquire_i32(u32 *ptr) {
  return __scoped_atomic_load_n(ptr, __ATOMIC_ACQUIRE,
                                __MEMORY_SCOPE_WVFRNT);
}

u64 load_seq_cst_i64(u64 *ptr) {
  return __scoped_atomic_load_n(ptr, __ATOMIC_SEQ_CST,
                                __MEMORY_SCOPE_SINGLE);
}

void load_generic_runtime_scope(u64 *ptr, u64 *result, int scope) {
  __scoped_atomic_load(ptr, result, __ATOMIC_ACQUIRE, scope);
}

void store_relaxed_i8(u8 *ptr, u8 value) {
  __scoped_atomic_store_n(ptr, value, __ATOMIC_RELAXED,
                          __MEMORY_SCOPE_CLUSTR);
}

void store_release_i16(u16 *ptr, u16 value) {
  __scoped_atomic_store_n(ptr, value, __ATOMIC_RELEASE,
                          __MEMORY_SCOPE_DEVICE);
}

void store_seq_cst_i32(u32 *ptr, u32 value) {
  __scoped_atomic_store_n(ptr, value, __ATOMIC_SEQ_CST,
                          __MEMORY_SCOPE_SYSTEM);
}

void store_release_i64(u64 *ptr, u64 value) {
  __scoped_atomic_store_n(ptr, value, __ATOMIC_RELEASE,
                          __MEMORY_SCOPE_WRKGRP);
}

void store_generic_runtime_scope(u64 *ptr, u64 *value, int scope) {
  __scoped_atomic_store(ptr, value, __ATOMIC_RELEASE, scope);
}

u8 exchange_relaxed_i8(u8 *ptr, u8 value) {
  return __scoped_atomic_exchange_n(ptr, value, __ATOMIC_RELAXED,
                                    __MEMORY_SCOPE_SYSTEM);
}

u16 exchange_consume_i16(u16 *ptr, u16 value) {
  return __scoped_atomic_exchange_n(ptr, value, __ATOMIC_CONSUME,
                                    __MEMORY_SCOPE_DEVICE);
}

u32 exchange_acquire_i32(u32 *ptr, u32 value) {
  return __scoped_atomic_exchange_n(ptr, value, __ATOMIC_ACQUIRE,
                                    __MEMORY_SCOPE_CLUSTR);
}

u32 exchange_release_i32(u32 *ptr, u32 value) {
  return __scoped_atomic_exchange_n(ptr, value, __ATOMIC_RELEASE,
                                    __MEMORY_SCOPE_WRKGRP);
}

u64 exchange_acq_rel_i64(u64 *ptr, u64 value) {
  return __scoped_atomic_exchange_n(ptr, value, __ATOMIC_ACQ_REL,
                                    __MEMORY_SCOPE_WVFRNT);
}

u8 exchange_seq_cst_i8(u8 *ptr, u8 value) {
  return __scoped_atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST,
                                    __MEMORY_SCOPE_SINGLE);
}

void exchange_generic_runtime_scope(u64 *ptr, u64 *value, u64 *result,
                                    int scope) {
  __scoped_atomic_exchange(ptr, value, result, __ATOMIC_SEQ_CST, scope);
}

void fence_relaxed(void) {
  __scoped_atomic_thread_fence(__ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
}

void fence_acquire(void) {
  __scoped_atomic_thread_fence(__ATOMIC_ACQUIRE, __MEMORY_SCOPE_DEVICE);
}

void fence_consume(void) {
  __scoped_atomic_thread_fence(__ATOMIC_CONSUME, __MEMORY_SCOPE_SINGLE);
}

void fence_release(void) {
  __scoped_atomic_thread_fence(__ATOMIC_RELEASE, __MEMORY_SCOPE_WRKGRP);
}

void fence_acq_rel(void) {
  __scoped_atomic_thread_fence(__ATOMIC_ACQ_REL, __MEMORY_SCOPE_WVFRNT);
}

void fence_seq_cst(void) {
  __scoped_atomic_thread_fence(__ATOMIC_SEQ_CST, __MEMORY_SCOPE_CLUSTR);
}
