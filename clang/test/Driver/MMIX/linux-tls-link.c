// REQUIRES: mmix-registered-target, lld
// RUN: split-file %s %t
// RUN: %python %S/Inputs/linux-tls-link.py %t llvm-ar llvm-readobj llvm-objdump %clang

// Runtime-free TLS composition. The entry returns an address for inspection;
// it is not a process entry implementation and must not be executed.

//--- consumer.c
extern _Thread_local long initialized[3];
extern _Thread_local long zero[3];
long *_start(void) { return initialized + 1; }
long *zero_address(void) { return zero + 2; }

//--- provider.c
_Alignas(ALIGN) _Thread_local long initialized[3] = {11, 22, 33};
_Alignas(ALIGN) _Thread_local long zero[3];

//--- wrong.c
long initialized[3] = {11, 22, 33};
long zero[3];

//--- unused.c
extern long missing(void);
long unused(void) { return missing(); }
