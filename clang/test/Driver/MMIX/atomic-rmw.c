// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang --target=mmix-unknown-unknown -ffreestanding -std=gnu17 -O1 \
// RUN:   -S %s -o - | FileCheck %s --check-prefix=ASM \
// RUN:   --implicit-check-not=__atomic_ --implicit-check-not=__sync_

// The fetch forms return the old value. Together these operations cover every
// native width and every integer atomicrmw operation in the MMIX contract.
// IR-LABEL: define {{.*}} i8 @c11_fetch_add_i8(
// IR: [[ADD_OLD:%.*]] = atomicrmw add {{.*}} i8 {{.*}} monotonic, align 1
// IR-NEXT: ret i8 [[ADD_OLD]]
// IR-LABEL: define {{.*}} i16 @c11_fetch_sub_i16(
// IR: [[SUB_OLD:%.*]] = atomicrmw sub {{.*}} i16 {{.*}} acquire, align 2
// IR-NEXT: ret i16 [[SUB_OLD]]
// IR-LABEL: define {{.*}} i32 @c11_fetch_and_i32(
// IR: [[AND_OLD:%.*]] = atomicrmw and {{.*}} i32 {{.*}} release, align 4
// IR-NEXT: ret i32 [[AND_OLD]]
// IR-LABEL: define {{.*}} i64 @c11_fetch_or_i64(
// IR: [[OR_OLD:%.*]] = atomicrmw or {{.*}} i64 {{.*}} acq_rel, align 8
// IR-NEXT: ret i64 [[OR_OLD]]
// IR-LABEL: define {{.*}} i8 @c11_fetch_xor_i8(
// IR: [[XOR_OLD:%.*]] = atomicrmw xor {{.*}} i8 {{.*}} seq_cst, align 1
// IR-NEXT: ret i8 [[XOR_OLD]]
// IR-LABEL: define {{.*}} i16 @c11_fetch_nand_i16(
// IR: [[NAND_OLD:%.*]] = atomicrmw nand {{.*}} i16 {{.*}} monotonic, align 2
// IR-NEXT: ret i16 [[NAND_OLD]]
// IR-LABEL: define {{.*}} i64 @c11_fetch_min_i64(
// IR: [[MIN_OLD:%.*]] = atomicrmw min {{.*}} i64 {{.*}} monotonic, align 8
// IR-NEXT: ret i64 [[MIN_OLD]]
// IR-LABEL: define {{.*}} i16 @c11_fetch_max_i16(
// IR: [[MAX_OLD:%.*]] = atomicrmw max {{.*}} i16 {{.*}} monotonic, align 2
// IR-NEXT: ret i16 [[MAX_OLD]]
// IR-LABEL: define {{.*}} i8 @c11_fetch_umin_i8(
// IR: [[UMIN_OLD:%.*]] = atomicrmw umin {{.*}} i8 {{.*}} monotonic, align 1
// IR-NEXT: ret i8 [[UMIN_OLD]]
// IR-LABEL: define {{.*}} i32 @c11_fetch_umax_i32(
// IR: [[UMAX_OLD:%.*]] = atomicrmw umax {{.*}} i32 {{.*}} monotonic, align 4
// IR-NEXT: ret i32 [[UMAX_OLD]]

// The GNU operation-and-fetch forms compute and return the new value.
// IR-LABEL: define {{.*}} i64 @gnu_add_fetch_i64(
// IR: [[GNU_ADD_OLD:%.*]] = atomicrmw add {{.*}} monotonic, align 8
// IR: [[GNU_ADD_NEW:%.*]] = add i64 [[GNU_ADD_OLD]],
// IR: ret i64 [[GNU_ADD_NEW]]
// IR-LABEL: define {{.*}} i64 @gnu_sub_fetch_i64(
// IR: [[GNU_SUB_OLD:%.*]] = atomicrmw sub {{.*}} monotonic, align 8
// IR: [[GNU_SUB_NEW:%.*]] = sub i64 [[GNU_SUB_OLD]],
// IR: ret i64 [[GNU_SUB_NEW]]
// IR-LABEL: define {{.*}} i64 @gnu_and_fetch_i64(
// IR: atomicrmw and {{.*}} monotonic, align 8
// IR: and i64
// IR-LABEL: define {{.*}} i64 @gnu_or_fetch_i64(
// IR: atomicrmw or {{.*}} monotonic, align 8
// IR: or i64
// IR-LABEL: define {{.*}} i64 @gnu_xor_fetch_i64(
// IR: atomicrmw xor {{.*}} monotonic, align 8
// IR: xor i64
// IR-LABEL: define {{.*}} i64 @gnu_nand_fetch_i64(
// IR: atomicrmw nand {{.*}} monotonic, align 8
// IR: and i64
// IR-NEXT: xor i64 {{.*}}, -1
// IR-LABEL: define {{.*}} i64 @gnu_min_fetch_i64(
// IR: atomicrmw min {{.*}} monotonic, align 8
// IR: call i64 @llvm.smin.i64
// IR-LABEL: define {{.*}} i64 @gnu_umax_fetch_i64(
// IR: atomicrmw umax {{.*}} monotonic, align 8
// IR: call i64 @llvm.umax.i64

// IR-LABEL: define {{.*}} i32 @language_postincrement(
// IR: [[POST_OLD:%.*]] = atomicrmw add {{.*}} i32 1 seq_cst, align 4
// IR-NEXT: ret i32 [[POST_OLD]]
// IR-LABEL: define {{.*}} i32 @language_add_assign(
// IR: [[ASSIGN_OLD:%.*]] = atomicrmw add {{.*}} seq_cst, align 4
// IR: [[ASSIGN_NEW:%.*]] = add i32 [[ASSIGN_OLD]],
// IR: ret i32 [[ASSIGN_NEW]]
// IR-LABEL: define {{.*}} ptr @c11_pointer_add(
// IR: [[SCALED:%.*]] = shl i64 {{.*}}, 2
// IR-NEXT: atomicrmw add {{.*}} i64 [[SCALED]] monotonic, align 8
// IR-LABEL: define {{.*}} ptr @gnu_pointer_add(
// IR-NOT: shl
// IR: atomicrmw add {{.*}} monotonic, align 8

// Narrow loops preserve the unselected bits of their containing octabyte.
// ASM-LABEL: c11_fetch_add_i8:
// ASM-NOT: SYNC
// ASM: LDOU
// ASM: ADDU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-NOT: SYNC
// ASM: POP 0, 0
// ASM-LABEL: c11_fetch_sub_i16:
// ASM: LDOU
// ASM: SUBU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: c11_fetch_and_i32:
// ASM: SYNC 3
// ASM: LDOU
// ASM: AND
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: c11_fetch_or_i64:
// ASM: SYNC 3
// ASM: LDOU
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: c11_fetch_xor_i8:
// ASM: SYNC 3
// ASM: LDOU
// ASM: XOR
// ASM: CSWAP
// ASM: BNZB
// ASM: SYNC 3
// ASM-LABEL: c11_fetch_nand_i16:
// ASM: LDOU
// ASM: AND
// ASM: ANDN
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: c11_fetch_min_i64:
// ASM: LDOU
// ASM: CMP
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: c11_fetch_max_i16:
// ASM: LDOU
// ASM: CMP
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: c11_fetch_umin_i8:
// ASM: LDOU
// ASM: CMPU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: c11_fetch_umax_i32:
// ASM: LDOU
// ASM: CMPU
// ASM: AND
// ASM: OR
// ASM: CSWAP
// ASM: BNZB

// Each GNU operation-and-fetch form also reaches the native retry loop.
// ASM-LABEL: gnu_add_fetch_i64:
// ASM: ADDU
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_sub_fetch_i64:
// ASM: SUBU
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_and_fetch_i64:
// ASM: AND
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_or_fetch_i64:
// ASM: OR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_xor_fetch_i64:
// ASM: XOR
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_nand_fetch_i64:
// ASM: NAND
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_min_fetch_i64:
// ASM: CMP
// ASM: CSWAP
// ASM: BNZB
// ASM-LABEL: gnu_umax_fetch_i64:
// ASM: CMPU
// ASM: CSWAP
// ASM: BNZB

typedef unsigned char u8;
typedef signed short i16;
typedef unsigned int u32;
typedef signed long i64;
typedef unsigned long u64;

u8 c11_fetch_add_i8(_Atomic(u8) *ptr, u8 value) {
  return __c11_atomic_fetch_add(ptr, value, __ATOMIC_RELAXED);
}

i16 c11_fetch_sub_i16(_Atomic(i16) *ptr, i16 value) {
  return __c11_atomic_fetch_sub(ptr, value, __ATOMIC_ACQUIRE);
}

u32 c11_fetch_and_i32(_Atomic(u32) *ptr, u32 value) {
  return __c11_atomic_fetch_and(ptr, value, __ATOMIC_RELEASE);
}

u64 c11_fetch_or_i64(_Atomic(u64) *ptr, u64 value) {
  return __c11_atomic_fetch_or(ptr, value, __ATOMIC_ACQ_REL);
}

u8 c11_fetch_xor_i8(_Atomic(u8) *ptr, u8 value) {
  return __c11_atomic_fetch_xor(ptr, value, __ATOMIC_SEQ_CST);
}

i16 c11_fetch_nand_i16(_Atomic(i16) *ptr, i16 value) {
  return __c11_atomic_fetch_nand(ptr, value, __ATOMIC_RELAXED);
}

i64 c11_fetch_min_i64(_Atomic(i64) *ptr, i64 value) {
  return __c11_atomic_fetch_min(ptr, value, __ATOMIC_RELAXED);
}

i16 c11_fetch_max_i16(_Atomic(i16) *ptr, i16 value) {
  return __c11_atomic_fetch_max(ptr, value, __ATOMIC_RELAXED);
}

u8 c11_fetch_umin_i8(_Atomic(u8) *ptr, u8 value) {
  return __c11_atomic_fetch_min(ptr, value, __ATOMIC_RELAXED);
}

u32 c11_fetch_umax_i32(_Atomic(u32) *ptr, u32 value) {
  return __c11_atomic_fetch_max(ptr, value, __ATOMIC_RELAXED);
}

i64 gnu_add_fetch_i64(i64 *ptr, i64 value) {
  return __atomic_add_fetch(ptr, value, __ATOMIC_RELAXED);
}

i64 gnu_sub_fetch_i64(i64 *ptr, i64 value) {
  return __atomic_sub_fetch(ptr, value, __ATOMIC_RELAXED);
}

u64 gnu_and_fetch_i64(u64 *ptr, u64 value) {
  return __atomic_and_fetch(ptr, value, __ATOMIC_RELAXED);
}

u64 gnu_or_fetch_i64(u64 *ptr, u64 value) {
  return __atomic_or_fetch(ptr, value, __ATOMIC_RELAXED);
}

u64 gnu_xor_fetch_i64(u64 *ptr, u64 value) {
  return __atomic_xor_fetch(ptr, value, __ATOMIC_RELAXED);
}

u64 gnu_nand_fetch_i64(u64 *ptr, u64 value) {
  return __atomic_nand_fetch(ptr, value, __ATOMIC_RELAXED);
}

i64 gnu_min_fetch_i64(i64 *ptr, i64 value) {
  return __atomic_min_fetch(ptr, value, __ATOMIC_RELAXED);
}

u64 gnu_umax_fetch_i64(u64 *ptr, u64 value) {
  return __atomic_max_fetch(ptr, value, __ATOMIC_RELAXED);
}

int language_postincrement(_Atomic(int) *ptr) { return (*ptr)++; }

int language_add_assign(_Atomic(int) *ptr, int value) {
  return *ptr += value;
}

int *c11_pointer_add(_Atomic(int *) *ptr, long count) {
  return __c11_atomic_fetch_add(ptr, count, __ATOMIC_RELAXED);
}

void *gnu_pointer_add(void **ptr, long bytes) {
  return __atomic_fetch_add(ptr, bytes, __ATOMIC_RELAXED);
}
