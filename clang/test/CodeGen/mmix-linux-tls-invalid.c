// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-linux -femulated-tls -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=EMULATED
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=invalid -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MODEL
// EMULATED: error: MMIX Linux does not support emulated TLS
// GENERIC: error: thread-local storage is not supported for the current target
// MODEL: error: invalid value 'invalid'

extern __thread long declaration;
long read(void) { return declaration; }
