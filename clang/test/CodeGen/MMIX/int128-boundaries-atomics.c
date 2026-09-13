// REQUIRES: mmix-registered-target
// Compile each operation separately: CodeGen stops after the first error.
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case1 -DCASE=1 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case1 -DCASE=1 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case1 -DCASE=1 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case1 -DCASE=1 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case2 -DCASE=2 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case2 -DCASE=2 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case2 -DCASE=2 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case2 -DCASE=2 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case3 -DCASE=3 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case3 -DCASE=3 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case3 -DCASE=3 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case3 -DCASE=3 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case4 -DCASE=4 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case4 -DCASE=4 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case4 -DCASE=4 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case4 -DCASE=4 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case5 -DCASE=5 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case5 -DCASE=5 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case5 -DCASE=5 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case5 -DCASE=5 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case6 -DCASE=6 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case6 -DCASE=6 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case6 -DCASE=6 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case6 -DCASE=6 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case7 -DCASE=7 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case7 -DCASE=7 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case7 -DCASE=7 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case7 -DCASE=7 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case8 -DCASE=8 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case8 -DCASE=8 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case8 -DCASE=8 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case8 -DCASE=8 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case9 -DCASE=9 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case9 -DCASE=9 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c -std=gnu17 -mrelocation-model static -emit-obj -o %t.o -verify=case9 -DCASE=9 -Wno-atomic-alignment -Wno-sync-alignment %s
// RUN: %clang_cc1 -triple mmix-unknown-unknown -x c++ -std=gnu++17 -mrelocation-model static -emit-obj -o %t.o -verify=case9 -DCASE=9 -Wno-atomic-alignment -Wno-sync-alignment %s

typedef __int128 I;
typedef unsigned __int128 U;
#if CASE == 1
I sync_add(I *p) {
  return __sync_fetch_and_add(p, 1); // case1-error {{MMIX GNU ABI does not support atomic builtin __sync_fetch_and_add_16}}
}
#endif
#if CASE == 2
U sync_cas(U *p) {
  return __sync_val_compare_and_swap(p, 0, 1); // case2-error {{MMIX GNU ABI does not support atomic builtin __sync_val_compare_and_swap_16}}
}
#endif
#if CASE == 3
I gnu_add(I *p) {
  return __atomic_fetch_add(p, 1, __ATOMIC_RELAXED); // case3-error {{MMIX GNU ABI does not support atomic operation __atomic_fetch_add}}
}
#endif
#if CASE == 4
U gnu_xor(U *p) {
  return __atomic_xor_fetch(p, 1, __ATOMIC_SEQ_CST); // case4-error {{MMIX GNU ABI does not support atomic operation __atomic_xor_fetch}}
}
#endif
#if CASE == 5
I scoped_add(I *p) {
  return __scoped_atomic_fetch_add(p, 1, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM); // case5-error {{MMIX GNU ABI does not support atomic operation __scoped_atomic_fetch_add}}
}
#endif
#if CASE == 6
I c11_add(_Atomic(I) *p) {
  return __c11_atomic_fetch_add(p, 1, __ATOMIC_RELAXED); // case6-error {{MMIX GNU ABI does not support atomic operation __c11_atomic_fetch_add}}
}
#endif
#if CASE == 7
U compound(_Atomic(U) *p) {
  return *p += 1; // case7-error {{MMIX GNU ABI does not support atomic operation +=}}
}
#endif
#if CASE == 8
I increment(_Atomic(I) *p) {
  return ++*p; // case8-error {{MMIX GNU ABI does not support atomic operation ++}}
}
#endif
#if CASE == 9
U decrement(_Atomic(U) *p) {
  return (*p)--; // case9-error {{MMIX GNU ABI does not support atomic operation --}}
}
#endif
