// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c++17 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c++17 -O0 -emit-obj -o %t.o %s
// RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c++17 -O2 -emit-obj -o %t.o2.o %s
// RUN: llvm-readobj -r %t.o2.o | FileCheck %s --check-prefix=OBJ

// Generated guards must use the selected model too, not merely named variables.
// Trivial destruction needs no thread-exit registration provider.
// CHECK-DAG: @constant = {{.*}}thread_local(localexec) global i64 7
// CHECK-DAG: @external = external thread_local(localexec) global i64
// CHECK-DAG: @_ZZ6objectvE5value = internal thread_local(localexec) global
// CHECK-DAG: @_ZGVZ6objectvE5value = internal thread_local(localexec) global i8 0
// CHECK-DAG: @__tls_guard = internal thread_local(localexec) global i8 0
// CHECK-LABEL: define {{.*}} @_Z6objectv(
// CHECK: load i8, ptr @_ZGVZ6objectvE5value
// CHECK: br i1
// CHECK: call void @_ZN6ObjectC1Ev
// CHECK: store i8 1, ptr @_ZGVZ6objectvE5value
// CHECK-LABEL: define internal void @__tls_init()
// CHECK: load i8, ptr @__tls_guard
// CHECK: br i1
// CHECK: store i8 1, ptr @__tls_guard
// CHECK: call void @__cxx_global_var_init
// OBJ: R_MMIX_TPREL_LO16
// OBJ: R_MMIX_TPREL_ML16
// OBJ: R_MMIX_TPREL_MH16
// OBJ: R_MMIX_TPREL_HI16

thread_local long constant = 7;
extern thread_local long external;
struct Object {
  Object();
  long value;
};
thread_local Object global_object;
Object &object() {
  static thread_local Object value;
  return value;
}
long read() { return constant + external + object().value; }
