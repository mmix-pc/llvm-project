// REQUIRES: mmix-registered-target
// RUN: %clang --target=mmix-unknown-linux -fpic -O0 -c %s -o %t.small.o
// RUN: %clang --target=mmix-unknown-linux -fPIC -O2 -c %s -o %t.large.o
// RUN: %clang --target=mmix-unknown-linux -fpie -O0 -c %s -o %t.pie-small.o
// RUN: %clang --target=mmix-unknown-linux -fPIE -O2 -c %s -o %t.pie-large.o
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model pic -pic-level 2 -emit-obj %s -o %t.cc1.o
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model pic -pic-level 2 -pic-is-pie -emit-obj %s -o %t.cc1-pie.o
// RUN: llvm-readobj -r %t.small.o %t.large.o %t.pie-small.o %t.pie-large.o %t.cc1.o %t.cc1-pie.o | FileCheck %s --check-prefix=OBJECT
// RUN: llvm-readobj -r %t.large.o | FileCheck %s --check-prefix=CALL
// RUN: %clang --target=mmix-unknown-linux -fPIC -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-linux -mrelocation-model pic -pic-level 2 -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// OBJECT-COUNT-6: R_MMIX_GOT_PC64 value 0x0
// CALL: R_MMIX_GOT_PC64 callee 0x0
// IR: define {{.*}}ptr @address(
// IR: !"PIC Level", i32 2

extern long value;
long *address(void) { return &value; }
extern long callee(long);
long call(long x) { return callee(x) + 1; }
