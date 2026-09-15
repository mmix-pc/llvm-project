// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -std=c++17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -O2 -emit-obj -o %t.linux.o %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -std=c++17 -O2 -emit-obj -o %t.generic.o %s

using size_t = __SIZE_TYPE__;
struct AllocationState { bool success; };
void *operator new(size_t, AllocationState &) noexcept;
void *operator new[](size_t, AllocationState &) noexcept;

extern "C" unsigned char *bytes(size_t size, AllocationState &state) {
  return new (state) unsigned char[size]();
}
// CHECK-LABEL: define{{.*}} @bytes(
// CHECK: call{{.*}}ptr @_ZnamR15AllocationState(i64{{.*}}, ptr{{.*}})
// CHECK: icmp eq ptr
// CHECK: br i1
// CHECK: call void @llvm.memset
// CHECK: ret ptr

extern "C" long *scalar(AllocationState &state) {
  return new (state) long(42);
}
// CHECK-LABEL: define{{.*}} @scalar(
// CHECK: call{{.*}}ptr @_ZnwmR15AllocationState(i64{{.*}}8, ptr{{.*}})
// CHECK: icmp eq ptr
// CHECK: br i1
// CHECK: store i64 42
// CHECK: ret ptr

extern "C" long *words(size_t size, AllocationState &state) {
  return new (state) long[size];
}
// CHECK-LABEL: define{{.*}} @words(
// CHECK: call { i64, i1 } @llvm.umul.with.overflow.i64
// CHECK: select i1 {{.*}}, i64 -1, i64
// CHECK: call{{.*}}ptr @_ZnamR15AllocationState

struct Block {
  unsigned char data[16] = {};
  Block *next = nullptr;
};
extern "C" Block *block(AllocationState &state) {
  return new (state) Block();
}
// CHECK-LABEL: define{{.*}} @block(
// CHECK: call{{.*}}ptr @_ZnwmR15AllocationState(i64{{.*}}24, ptr{{.*}})
// CHECK: icmp eq ptr
// CHECK: br i1
// CHECK: call void @llvm.memset
// CHECK: call{{.*}} @_ZN5BlockC1Ev
// CHECK: ret ptr

struct Element {
  long value;
  Element() : value(7) {}
};
extern "C" Element *elements(size_t count, AllocationState &state) {
  return new (state) Element[count];
}
// CHECK-LABEL: define{{.*}} @elements(
// CHECK: call { i64, i1 } @llvm.umul.with.overflow.i64(i64 {{.*}}, i64 8)
// CHECK: call{{.*}}ptr @_ZnamR15AllocationState
// CHECK: icmp eq ptr
// CHECK: br i1
// CHECK: call{{.*}} @_ZN7ElementC1Ev
// CHECK: ret ptr
