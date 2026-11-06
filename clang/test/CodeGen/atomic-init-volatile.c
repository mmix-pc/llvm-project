// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c11 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple mmix-unknown-linux -std=c11 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c11 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -std=c11 -emit-llvm -o - %s | FileCheck %s

// CHECK-LABEL: define{{.*}} @scalar(
// CHECK: store volatile i32
void scalar(volatile _Atomic(int) *p, int value) {
  __c11_atomic_init(p, value);
}

// CHECK-LABEL: define{{.*}} @complex_value(
// CHECK: store volatile float
// CHECK: store volatile float
void complex_value(volatile _Atomic(float _Complex) *p, float _Complex value) {
  __c11_atomic_init(p, value);
}

struct aggregate { int x, y; };
// CHECK-LABEL: define{{.*}} @aggregate_value(
// CHECK: call void @llvm.memcpy{{.*}}i1 true)
void aggregate_value(volatile _Atomic(struct aggregate) *p,
                     struct aggregate value) {
  __c11_atomic_init(p, value);
}

// CHECK-LABEL: define{{.*}} @ordinary(
// CHECK-NOT: volatile
// CHECK: ret void
void ordinary(_Atomic(int) *p, int value) {
  __c11_atomic_init(p, value);
}

// OPT-LABEL: define{{.*}} @repeated(
// OPT: store volatile i32 1
// OPT: store volatile i32 2
void repeated(volatile _Atomic(int) *p) {
  __c11_atomic_init(p, 1);
  __c11_atomic_init(p, 2);
}
