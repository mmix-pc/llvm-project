// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=c17 -ffreestanding -emit-llvm -O2 -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -std=c17 -ffreestanding -emit-llvm -O2 -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -x c++ -std=c++17 -ffreestanding -emit-llvm -O2 -o - %s | FileCheck %s
#include <stdatomic.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern _Atomic(int) *next_object(void);
// The query inspects the type without accessing the object, but must still
// evaluate the supplied pointer expression exactly once.
// CHECK-LABEL: define{{.*}} @query(
// CHECK: call{{.*}} @next_object(
// CHECK-NOT: call{{.*}} @next_object(
// CHECK-NOT: load
// CHECK: ret i1 true
bool query(void) { return atomic_is_lock_free(next_object()); }
// CHECK-LABEL: define{{.*}} @null_query(
// CHECK-NOT: load
// CHECK: ret i1 true
bool null_query(void) { return atomic_is_lock_free((_Atomic(int) *)0); }
#ifdef __cplusplus
}
#endif
