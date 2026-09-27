// REQUIRES: mmix-registered-target
// RUN: not %clang_cc1 -triple mmix-unknown-linux -mrelocation-model static -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -ftls-model=global-dynamic -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -ftls-model=local-dynamic -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -ftls-model=initial-exec -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -ftls-model=local-exec -ftls-model=global-dynamic -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -femulated-tls -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=EMULATED
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -mrelocation-model pic -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -pic-level 2 -pic-is-pie -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -emit-llvm -o /dev/null -DATTR=\"global-dynamic\" %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -emit-llvm -o /dev/null -DATTR=\"local-dynamic\" %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -emit-llvm -o /dev/null -DATTR=\"initial-exec\" %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-unknown -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=GENERIC
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -emit-llvm -o /dev/null -DDEFINITION_ONLY -DATTR=\"initial-exec\" %s 2>&1 | FileCheck %s --check-prefix=MODEL
// RUN: not %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -emit-llvm -o /dev/null -DLOCAL_ONLY -DATTR=\"local-dynamic\" %s 2>&1 | FileCheck %s --check-prefix=MODEL
// MODEL: error: MMIX Linux supports only the local-exec TLS model
// EMULATED: error: MMIX Linux does not support emulated TLS
// PIC: error: MMIX Linux requires static non-PIE code
// GENERIC: error: thread-local storage is not supported for the current target

#ifndef ATTR
#define ATTR "local-exec"
#endif
#if defined(DEFINITION_ONLY)
__thread long definition __attribute__((tls_model(ATTR)));
#elif defined(LOCAL_ONLY)
long *address(void) {
  static __thread long local __attribute__((tls_model(ATTR)));
  return &local;
}
#else
extern __thread long declaration __attribute__((tls_model(ATTR)));
long read(void) { return declaration; }
#endif
