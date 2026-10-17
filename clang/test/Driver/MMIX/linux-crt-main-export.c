// REQUIRES: mmix-registered-target, lld
// RUN: %clang --target=mmix-unknown-linux -fPIC -O0 -c %S/../../../../libc/startup/linux/mmix/dynamic_start.cpp -o %t.crt0.o
// RUN: %clang --target=mmix-unknown-linux -fPIC -O2 -c %S/../../../../libc/startup/linux/mmix/dynamic_start.cpp -o %t.crt2.o
// RUN: %clang --target=mmix-unknown-linux -nostdlib -rdynamic %s %t.crt0.o -o %t.pie0
// RUN: %clang --target=mmix-unknown-linux -nostdlib -rdynamic %s %t.crt2.o -o %t.pie2
// RUN: llvm-readobj --dyn-symbols %t.pie0 %t.pie2 | FileCheck %s
// CHECK: Name: main (
// CHECK: Binding: Global
// CHECK: Other: 0
// CHECK: Name: main (
// CHECK: Binding: Global
// CHECK: Other: 0

// A runtime-free link probe; actual startup is tested with the shared runtime.
int main(int argc, char **argv, char **envp) { return argc; }
__attribute__((noreturn)) void
__llvm_libc_mmix_dynamic_start(int (*entry)(int, char **, char **), const void *sp) {
  for (;;) __asm__ volatile("");
}
