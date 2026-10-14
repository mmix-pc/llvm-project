// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -emit-llvm -o /dev/null %s

// Alias emission can precede the tentative definition's TLS attributes.
_Thread_local long value;
extern _Thread_local long local_value
    __attribute__((alias("value"), visibility("hidden")));

// CHECK-DAG: @value ={{.*}} thread_local global i64 0
// CHECK-DAG: @local_value = hidden thread_local alias i64, ptr @value
// CHECK-DAG: @forward ={{.*}} thread_local(initialexec) global i64 0
// CHECK-DAG: @forward_alias ={{.*}} thread_local alias i64, ptr @forward

// CHECK-LABEL: define{{.*}} ptr @address(
// CHECK: call{{.*}} ptr @llvm.threadlocal.address.p0(ptr @value)
long *address(void) { return &value; }

// CHECK-LABEL: define{{.*}} ptr @alias_address(
// CHECK: call{{.*}} ptr @llvm.threadlocal.address.p0(ptr @local_value)
long *alias_address(void) { return &local_value; }

extern _Thread_local long forward __attribute__((tls_model("initial-exec")));
extern _Thread_local long forward_alias __attribute__((alias("forward")));

// CHECK-LABEL: define{{.*}} ptr @forward_address(
// CHECK: call{{.*}} ptr @llvm.threadlocal.address.p0(ptr @forward)
long *forward_address(void) { return &forward; }
_Thread_local long forward;
