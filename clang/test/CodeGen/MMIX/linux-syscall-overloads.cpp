// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -I %S/../../../../libc -DLIBC_NAMESPACE=__llvm_libc -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang --target=mmix-unknown-linux -nostdinc++ -I %S/../../../../libc -DLIBC_NAMESPACE=__llvm_libc -O0 -c %s -o %t.O0.o
// RUN: %clang --target=mmix-unknown-linux -nostdinc++ -I %S/../../../../libc -DLIBC_NAMESPACE=__llvm_libc -O2 -c %s -o %t.O2.o
// RUN: not %clang_cc1 -triple mmix-unknown-linux -I %S/../../../../libc -DLIBC_NAMESPACE=__llvm_libc -DTOO_MANY -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=ERROR

#include "src/__support/OSUtil/linux/syscall.h"

using LIBC_NAMESPACE::syscall_impl;
using LIBC_NAMESPACE::linux_syscalls::syscall_checked;

extern "C" {
// CHECK-LABEL: define{{.*}} @arity0(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0)
long arity0() { return syscall_impl<long>(472); }
// CHECK-LABEL: define{{.*}} @arity1(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 11, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0)
long arity1() { return syscall_impl<long>(472, 11); }
// CHECK-LABEL: define{{.*}} @arity2(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 11, i64{{.*}} 22, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0)
long arity2() { return syscall_impl<long>(472, 11, 22); }
// CHECK-LABEL: define{{.*}} @arity3(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 11, i64{{.*}} 22, i64{{.*}} 33, i64{{.*}} 0, i64{{.*}} 0, i64{{.*}} 0)
long arity3() { return syscall_impl<long>(472, 11, 22, 33); }
// CHECK-LABEL: define{{.*}} @arity4(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 11, i64{{.*}} 22, i64{{.*}} 33, i64{{.*}} 44, i64{{.*}} 0, i64{{.*}} 0)
long arity4() { return syscall_impl<long>(472, 11, 22, 33, 44); }
// CHECK-LABEL: define{{.*}} @arity5(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 11, i64{{.*}} 22, i64{{.*}} 33, i64{{.*}} 44, i64{{.*}} 55, i64{{.*}} 0)
long arity5() { return syscall_impl<long>(472, 11, 22, 33, 44, 55); }
// CHECK-LABEL: define{{.*}} @arity6(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} 472, i64{{.*}} 11, i64{{.*}} 22, i64{{.*}} 33, i64{{.*}} 44, i64{{.*}} 55, i64{{.*}} 66)
long arity6() { return syscall_impl<long>(472, 11, 22, 33, 44, 55, 66); }

// CHECK-LABEL: define{{.*}} @conversions(
// CHECK-DAG: sext i8 {{.*}} to i64
// CHECK-DAG: zext i8 {{.*}} to i64
// CHECK-DAG: sext i16 {{.*}} to i64
// CHECK-DAG: zext i16 {{.*}} to i64
// CHECK-DAG: ptrtoint ptr {{.*}} to i64
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(i64{{.*}} -81985529216486896,
long conversions(signed char a, unsigned char b, short c, unsigned short d,
                 void *p, unsigned long wide) {
  return syscall_impl<long>(static_cast<long>(0xfedcba9876543210UL),
                            a, b, c, d, p, wide);
}

// Ordinary, nonvolatile memory cannot move through the external syscall call.
// CHECK-LABEL: define{{.*}} @memory_effects(
// CHECK: store i64 7, ptr
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(
// CHECK: load i64, ptr
long memory_effects(long *p) {
  *p = 7;
  long result = syscall_impl<long>(472, p);
  return *p + result;
}

// CHECK-LABEL: define{{.*}} @discarded_result(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(
// CHECK: ret void
void discarded_result() { syscall_impl<long>(472); }

// CHECK-LABEL: define{{.*}} @checked_result(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(
long checked_result(long number) {
  auto result = syscall_checked<long>(number);
  return result ? *result : -result.error();
}

// CHECK-LABEL: define{{.*}} @checked_pointer(
// CHECK: call{{.*}} i64 @__llvm_libc_mmix_syscall(
void *checked_pointer(long number) {
  auto result = syscall_checked<void *>(number);
  return result ? *result : nullptr;
}
}

#ifdef TOO_MANY
// ERROR: static assertion failed{{.*}}Too many arguments for syscall
long rejected() { return syscall_impl<long>(1, 1, 2, 3, 4, 5, 6, 7); }
#endif
