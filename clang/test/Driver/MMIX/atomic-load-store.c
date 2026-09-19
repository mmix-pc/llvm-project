// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=c17 -O1 \
// RUN:   -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=c17 -O1 \
// RUN:   -S %s -o - | FileCheck %s --check-prefix=ASM \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_

// IR-LABEL: define{{.*}} i8 @c11_load_relaxed_i8(
// IR: load atomic i8, ptr %{{.*}} monotonic, align 1
// IR-LABEL: define{{.*}} i16 @c11_load_acquire_i16(
// IR: load atomic i16, ptr %{{.*}} acquire, align 2
// IR-LABEL: define{{.*}} i32 @gnu_load_relaxed_i32(
// IR: load atomic i32, ptr %{{.*}} monotonic, align 4
// IR-LABEL: define{{.*}} i64 @gnu_load_seq_cst_i64(
// IR: load atomic i64, ptr %{{.*}} seq_cst, align 8
// IR-LABEL: define{{.*}} void @c11_store_relaxed_i8(
// IR: store atomic i8 %{{.*}}, ptr %{{.*}} monotonic, align 1
// IR-LABEL: define{{.*}} void @c11_store_release_i16(
// IR: store atomic i16 %{{.*}}, ptr %{{.*}} release, align 2
// IR-LABEL: define{{.*}} void @gnu_store_relaxed_i32(
// IR: store atomic i32 %{{.*}}, ptr %{{.*}} monotonic, align 4
// IR-LABEL: define{{.*}} void @gnu_store_seq_cst_i64(
// IR: store atomic i64 %{{.*}}, ptr %{{.*}} seq_cst, align 8
// IR-LABEL: define{{.*}} void @gnu_generic_load(
// IR: load atomic i64, ptr %{{.*}} acquire, align 8
// IR-LABEL: define{{.*}} void @gnu_generic_store(
// IR: store atomic i64 %{{.*}}, ptr %{{.*}} release, align 8
// IR-LABEL: define{{.*}} i64 @language_load(
// IR: load atomic i64, ptr %{{.*}} seq_cst, align 8
// IR-LABEL: define{{.*}} void @language_store(
// IR: store atomic i64 %{{.*}}, ptr %{{.*}} seq_cst, align 8
// IR-LABEL: define{{.*}} i32 @volatile_load(
// IR: load atomic volatile i32, ptr %{{.*}} acquire, align 4
// IR-LABEL: define{{.*}} void @volatile_store(
// IR: store atomic volatile i32 %{{.*}}, ptr %{{.*}} release, align 4
// IR-LABEL: define{{.*}} void @thread_fence_relaxed(
// IR-NOT: fence
// IR: ret void
// IR-LABEL: define{{.*}} void @thread_fence_consume(
// IR: fence acquire
// IR-LABEL: define{{.*}} void @thread_fence_acquire(
// IR: fence acquire
// IR-LABEL: define{{.*}} void @thread_fence_release(
// IR: fence release
// IR-LABEL: define{{.*}} void @thread_fence_acq_rel(
// IR: fence acq_rel
// IR-LABEL: define{{.*}} void @thread_fence_seq_cst(
// IR: fence seq_cst
// IR-LABEL: define{{.*}} void @signal_fence_seq_cst(
// IR: fence syncscope("singlethread") seq_cst

// ASM-LABEL: c11_load_relaxed_i8:
// ASM-NOT: SYNC
// ASM-NOT: CSWAP
// ASM: LDBU r231, r231, 0
// ASM-NOT: CSWAP
// ASM-NOT: SYNC
// ASM: POP 0, 0

// ASM-LABEL: c11_load_acquire_i16:
// ASM-NOT: SYNC
// ASM-NOT: CSWAP
// ASM: LDWU r231, r231, 0
// ASM-NEXT: SYNC 3

// ASM-LABEL: gnu_load_relaxed_i32:
// ASM-NOT: SYNC
// ASM-NOT: CSWAP
// ASM: LDTU r231, r231, 0
// ASM-NOT: CSWAP
// ASM-NOT: SYNC
// ASM: POP 0, 0

// ASM-LABEL: gnu_load_seq_cst_i64:
// ASM: SYNC 3
// ASM: LDOU r231, r231, 0
// ASM-NEXT: SYNC 3

// ASM-LABEL: c11_store_relaxed_i8:
// ASM-NOT: SYNC
// ASM: ANDN [[STORE8:r[0-9]+]], r231, 7
// ASM: LDOU {{r[0-9]+}}, [[STORE8]], 0
// ASM: NXOR [[PRESERVE8:r[0-9]+]], {{r[0-9]+}}, 0
// ASM: AND {{r[0-9]+}}, {{r[0-9]+}}, [[PRESERVE8]]
// ASM: OR
// ASM: CSWAP r255, [[STORE8]], 0
// ASM: BNZB
// ASM-NOT: SYNC
// ASM: POP 0, 0

// ASM-LABEL: c11_store_release_i16:
// ASM: SYNC 3
// ASM: LDOU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB

// ASM-LABEL: gnu_store_relaxed_i32:
// ASM-NOT: SYNC
// ASM: LDOU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-NOT: SYNC
// ASM: POP 0, 0

// ASM-LABEL: gnu_store_seq_cst_i64:
// ASM: SYNC 3
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3

// ASM-LABEL: thread_fence_relaxed:
// ASM-NOT: SYNC
// ASM: POP 0, 0
// ASM-LABEL: thread_fence_consume:
// ASM: SYNC 3
// ASM-LABEL: thread_fence_acquire:
// ASM: SYNC 3
// ASM-LABEL: thread_fence_release:
// ASM: SYNC 3
// ASM-LABEL: thread_fence_acq_rel:
// ASM: SYNC 3
// ASM-LABEL: thread_fence_seq_cst:
// ASM: SYNC 3
// ASM-LABEL: signal_fence_seq_cst:
// ASM-NOT: SYNC
// ASM: POP 0, 0

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

u8 c11_load_relaxed_i8(_Atomic(u8) *ptr) {
  return __c11_atomic_load(ptr, __ATOMIC_RELAXED);
}

u16 c11_load_acquire_i16(_Atomic(u16) *ptr) {
  return __c11_atomic_load(ptr, __ATOMIC_ACQUIRE);
}

u32 gnu_load_relaxed_i32(u32 *ptr) {
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

u64 gnu_load_seq_cst_i64(u64 *ptr) {
  return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

void c11_store_relaxed_i8(_Atomic(u8) *ptr, u8 value) {
  __c11_atomic_store(ptr, value, __ATOMIC_RELAXED);
}

void c11_store_release_i16(_Atomic(u16) *ptr, u16 value) {
  __c11_atomic_store(ptr, value, __ATOMIC_RELEASE);
}

void gnu_store_relaxed_i32(u32 *ptr, u32 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

void gnu_store_seq_cst_i64(u64 *ptr, u64 value) {
  __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

void gnu_generic_load(u64 *ptr, u64 *result) {
  __atomic_load(ptr, result, __ATOMIC_ACQUIRE);
}

void gnu_generic_store(u64 *ptr, u64 *value) {
  __atomic_store(ptr, value, __ATOMIC_RELEASE);
}

u64 language_load(_Atomic(u64) *ptr) { return *ptr; }

void language_store(_Atomic(u64) *ptr, u64 value) { *ptr = value; }

u32 volatile_load(volatile _Atomic(u32) *ptr) {
  return __c11_atomic_load(ptr, __ATOMIC_ACQUIRE);
}

void volatile_store(volatile _Atomic(u32) *ptr, u32 value) {
  __c11_atomic_store(ptr, value, __ATOMIC_RELEASE);
}

void thread_fence_relaxed(void) {
  __c11_atomic_thread_fence(__ATOMIC_RELAXED);
}

void thread_fence_consume(void) {
  __c11_atomic_thread_fence(__ATOMIC_CONSUME);
}

void thread_fence_acquire(void) {
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

void thread_fence_release(void) {
  __atomic_thread_fence(__ATOMIC_RELEASE);
}

void thread_fence_acq_rel(void) {
  __atomic_thread_fence(__ATOMIC_ACQ_REL);
}

void thread_fence_seq_cst(void) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void signal_fence_seq_cst(void) {
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
}
