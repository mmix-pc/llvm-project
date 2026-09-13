// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -O0 -emit-obj -o %t.linux-o0.o %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -O2 -emit-obj -o %t.linux-o2.o %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -O0 -emit-obj -o %t.generic-o0.o %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -mrelocation-model static -O2 -emit-obj -o %t.generic-o2.o %s

void clobber(void);

// CHECK-LABEL: define {{.*}}ptr @caller_address(
// CHECK: call ptr @llvm.returnaddress.p0(i32 0)
void *caller_address(void) {
  return __builtin_return_address(0);
}

// CHECK-LABEL: define {{.*}}ptr @after_call(
// CHECK: call void @clobber()
// CHECK: call ptr @llvm.returnaddress.p0(i32 0)
void *after_call(void) {
  clobber();
  return __builtin_return_address(0);
}
