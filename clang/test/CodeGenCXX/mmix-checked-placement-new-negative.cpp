// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DCASE=0 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DCASE=1 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DCASE=2 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DCASE=3 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -std=c++17 -DCASE=4 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
using size_t = __SIZE_TYPE__;
struct State {};
#if CASE == 0
void *operator new[](size_t, State &);
void test(State &s) { new (s) char[10]; }
#elif CASE == 1
struct Object { Object(); ~Object(); };
void *operator new[](size_t, State &) noexcept;
void test(State &s) { new (s) Object[10]; }
#elif CASE == 2
void *operator new[](size_t, State &, int) noexcept;
void test(State &s) { new (s, 1) char[10]; }
#elif CASE == 3
void *operator new[](size_t, State &&) noexcept;
void test() { new (State{}) char[10]; }
#else
void *operator new[](size_t, State &, ...) noexcept;
void test(State &s) { new (s) char[10]; }
#endif
// CHECK: error: MMIX does not support C++ allocation form
