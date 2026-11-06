// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c17 -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -std=c17 -O2 -emit-llvm -o - %s | FileCheck %s

typedef __PTRDIFF_TYPE__ ptrdiff_t;

// VLA extents are captured on entry, even when the source variable changes.
// CHECK-LABEL: define{{.*}} @add_captured(
// CHECK: [[EXT:%.*]] = zext i32 %n to i64
// CHECK: [[BYTES:%.*]] = shl nuw nsw i64 [[EXT]], 2
// CHECK: atomicrmw add ptr %p, i64 [[BYTES]] seq_cst
void add_captured(int n, _Atomic(int (*)[n]) *p) {
  n = 7;
  __c11_atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST);
}

// Multiplication of the signed offset must not gain overflow promises.
// CHECK-LABEL: define{{.*}} @subtract(
// CHECK: [[EXT:%.*]] = zext i32 %n to i64
// CHECK: [[BYTES:%.*]] = shl nuw nsw i64 [[EXT]], 2
// CHECK: [[OFFSET:%.*]] = mul i64 [[BYTES]], %amount
// CHECK: atomicrmw sub ptr %p, i64 [[OFFSET]] acquire
void subtract(int n, _Atomic(int (*)[n]) *p, ptrdiff_t amount) {
  __c11_atomic_fetch_sub(p, amount, __ATOMIC_ACQUIRE);
}

// Both dynamic dimensions and the element size contribute to the stride.
// CHECK-LABEL: define{{.*}} @matrix(
// CHECK: [[N:%.*]] = zext i32 %n to i64
// CHECK: [[M:%.*]] = zext i32 %m to i64
// CHECK: [[BASE:%.*]] = shl nuw nsw i64 [[N]], 2
// CHECK: [[BYTES:%.*]] = mul i64 [[BASE]], [[M]]
// CHECK: atomicrmw volatile add ptr %p, i64 [[BYTES]] release
void matrix(int n, int m, volatile _Atomic(int (*)[n][m]) *p) {
  __c11_atomic_fetch_add(p, 1, __ATOMIC_RELEASE);
}

// GNU pointer atomics retain their byte-offset semantics.
// CHECK-LABEL: define{{.*}} @gnu_bytes(
// CHECK: atomicrmw add ptr %p, i64 %amount seq_cst
void gnu_bytes(int n, int (**p)[n], ptrdiff_t amount) {
  __atomic_fetch_add(p, amount, __ATOMIC_SEQ_CST);
}

// Constant-sized array pointers keep their constant stride.
// CHECK-LABEL: define{{.*}} @constant_array(
// CHECK: atomicrmw sub ptr %p, i64 12 monotonic
void constant_array(_Atomic(int (*)[3]) *p) {
  __c11_atomic_fetch_sub(p, 1, __ATOMIC_RELAXED);
}
