// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -ffreestanding -I %S/../../../../libc -DLIBC_FULL_BUILD -DLIBC_NAMESPACE=errno_test -DLIBC_ERRNO_MODE=LIBC_ERRNO_MODE_SHARED -DLIBC_THREAD_MODE=LIBC_THREAD_MODE_SINGLE -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -ffreestanding -I %S/../../../../libc -DLIBC_FULL_BUILD -DLIBC_NAMESPACE=errno_test -DLIBC_ERRNO_MODE=LIBC_ERRNO_MODE_SHARED -DLIBC_THREAD_MODE=LIBC_THREAD_MODE_SINGLE -O2 -emit-llvm -o - %s | FileCheck %s

// Storage code does not depend on Linux error numbers. Use libc's generic
// constants here so this compiler test does not require external Linux UAPI.
#undef __linux__
#include "src/errno/libc_errno.cpp"

// CHECK-NOT: thread_local
// CHECK: @_ZN10errno_test12_GLOBAL__N_112shared_errnoE = internal global i32 0
// CHECK: define{{.*}} ptr @__llvm_libc_errno()
// CHECK: ret ptr @_ZN10errno_test12_GLOBAL__N_112shared_errnoE
// CHECK-NOT: thread_local

extern "C" int errno_round_trip(int value) {
  libc_errno = value;
  return libc_errno;
}
