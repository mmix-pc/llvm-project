// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -O2 -emit-llvm -o /dev/null %s

extern thread_local int value;
extern thread_local int value_alias __attribute__((alias("value")));

int *address() { return &value; }

int initialize();
thread_local int value = initialize();

// CHECK-DAG: @value ={{.*}} thread_local global i32 0
// CHECK-DAG: @value_alias ={{.*}} thread_local alias i32, ptr @value
// CHECK: define{{.*}} void @__tls_init()
// CHECK: call void @__cxx_global_var_init()
