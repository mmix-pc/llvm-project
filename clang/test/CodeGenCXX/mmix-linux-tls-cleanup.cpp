// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -fcxx-exceptions -fexceptions -exception-model=dwarf -emit-llvm -o %t.ll %s
// RUN: FileCheck %s --input-file=%t.ll
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -fcxx-exceptions -fexceptions -exception-model=dwarf -emit-obj -o %t.o %s
// RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -ftls-model=local-exec -std=c++17 -fcxx-exceptions -fexceptions -exception-model=dwarf -O2 -emit-obj -o %t.opt.o %s
// RUN: llvm-readobj -r %t.opt.o | FileCheck %s --check-prefix=OBJ

struct Object { Object(); ~Object(); long value; };
thread_local Object global;
thread_local Object array[2];
thread_local const Object &temporary = Object();
Object &local() {
  try {
    static thread_local Object value;
    return value;
  } catch (...) {
    throw;
  }
}

// Generated guards and lifetime-extended temporaries use local-exec TLS too.
// Registration and guard publication follow successful construction.
// CHECK-DAG: @global = {{.*}}thread_local(localexec) global
// CHECK-DAG: @array = {{.*}}thread_local(localexec) global
// CHECK-DAG: @temporary = {{.*}}thread_local(localexec) global
// CHECK-DAG: @_ZGR9temporary_ = {{.*}}thread_local(localexec) global
// CHECK-DAG: @_ZGVZ5localvE5value = internal thread_local(localexec) global i8 0
// CHECK-LABEL: define internal void @__cxx_global_var_init()
// CHECK: call void @_ZN6ObjectC1Ev
// CHECK: call i32 @__cxa_thread_atexit(ptr @_ZN6ObjectD1Ev, ptr {{.*}}, ptr @__dso_handle)
// CHECK-LABEL: define {{.*}} @_Z5localv()
// CHECK: load i8, ptr @_ZGVZ5localvE5value
// CHECK: br i1
// CHECK: invoke void @_ZN6ObjectC1Ev
// CHECK-NEXT: to label %[[READY:[^ ]+]] unwind label %[[FAIL:[^ ]+]]
// CHECK: [[READY]]:
// CHECK: call i32 @__cxa_thread_atexit
// CHECK: store i8 1, ptr @_ZGVZ5localvE5value
// CHECK: [[FAIL]]:
// CHECK: landingpad
// CHECK-NOT: __cxa_thread_atexit
// CHECK-NOT: store i8 1
// CHECK: resume
// OBJ: R_MMIX_TPREL_LO16
// OBJ: R_MMIX_TPREL_ML16
// OBJ: R_MMIX_TPREL_MH16
// OBJ: R_MMIX_TPREL_HI16
// OBJ: __cxa_thread_atexit
