// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple mmix-unknown-linux -std=c++17 \
// RUN:   -mrelocation-model static -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=LINUX
using size_t = decltype(sizeof(0));

struct Arena {
  static void *operator new(size_t, unsigned long);
};

Arena *allocate(unsigned long Tag) { return new (Tag) Arena; }
// CHECK: error: MMIX does not support C++ allocation form
// LINUX: call{{.*}} @_ZN5ArenanwEmm
