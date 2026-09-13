// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -Wno-atomic-alignment -emit-obj -o %t.o %s
// RUN: llvm-nm --undefined-only %t.o | FileCheck %s --implicit-check-not=__atomic_load_16 --implicit-check-not=__atomic_store_16 --implicit-check-not=__atomic_exchange_16 --implicit-check-not=__atomic_compare_exchange_16
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -Wno-atomic-alignment -emit-obj -o %t.o %s
// RUN: llvm-nm --undefined-only %t.o | FileCheck %s --implicit-check-not=__atomic_load_16 --implicit-check-not=__atomic_store_16 --implicit-check-not=__atomic_exchange_16 --implicit-check-not=__atomic_compare_exchange_16
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -Wno-atomic-alignment -emit-obj -o %t.o %s
// RUN: llvm-nm --undefined-only %t.o | FileCheck %s --implicit-check-not=__atomic_load_16 --implicit-check-not=__atomic_store_16 --implicit-check-not=__atomic_exchange_16 --implicit-check-not=__atomic_compare_exchange_16
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -Wno-atomic-alignment -emit-obj -o %t.o %s
// RUN: llvm-nm --undefined-only %t.o | FileCheck %s --implicit-check-not=__atomic_load_16 --implicit-check-not=__atomic_store_16 --implicit-check-not=__atomic_exchange_16 --implicit-check-not=__atomic_compare_exchange_16

// These remain size-based copy helpers, not a new scalar atomic RMW ABI.
// CHECK-DAG: U __atomic_load
// CHECK-DAG: U __atomic_store
// CHECK-DAG: U __atomic_exchange
// CHECK-DAG: U __atomic_compare_exchange
typedef __int128 I;
I load(I *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
void store(I *p, I value) { __atomic_store_n(p, value, __ATOMIC_RELEASE); }
I exchange(I *p, I value) {
  return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST);
}
int compare(I *p, I *expected, I desired) {
  return __atomic_compare_exchange_n(p, expected, desired, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
}
