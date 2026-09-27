// REQUIRES: mmix-registered-target
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c11 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-linux-unknown -ftls-model=local-exec -mrelocation-model static -std=c11 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -round-trip-args -std=c11 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c11 -ftls-model=global-dynamic -ftls-model=local-exec -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c11 -emit-obj -o %t.o %s
// RUN: llvm-readobj --sections --relocations %t.o | FileCheck %s --check-prefix=OBJ
// RUN: %clang_cc1 -triple mmix-unknown-linux -ftls-model=local-exec -mrelocation-model static -std=c11 -O2 -emit-obj -o %t.o2.o %s
// RUN: llvm-readobj --relocations %t.o2.o | FileCheck %s --check-prefix=RELOC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -emit-llvm -o - %s | FileCheck %s --check-prefix=OTHER

// IR-DAG: @initialized = {{.*}}thread_local(localexec) global i64 42
// IR-DAG: @zero = {{.*}}thread_local(localexec) global i64 0
// IR-DAG: @decl = external thread_local(localexec) global i64
// IR-DAG: @explicit_model = {{.*}}thread_local(localexec) global i64 7
// IR-DAG: @local_address.local = internal thread_local(localexec) global i64 0
// IR: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@initialized)
// IR: call {{.*}}ptr @llvm.thread.pointer.p0()
// OTHER-DAG: @initialized = thread_local global i64 42
// OTHER-DAG: @decl = external thread_local global i64
// OBJ: Name: .tdata
// OBJ: SHF_TLS
// OBJ: Name: .tbss
// OBJ: SHF_TLS
// OBJ: R_MMIX_TPREL_LO16 initialized
// OBJ-NEXT: R_MMIX_TPREL_ML16 initialized
// OBJ-NEXT: R_MMIX_TPREL_MH16 initialized
// OBJ-NEXT: R_MMIX_TPREL_HI16 initialized
// RELOC: R_MMIX_TPREL_LO16 initialized
// RELOC-NEXT: R_MMIX_TPREL_ML16 initialized
// RELOC-NEXT: R_MMIX_TPREL_MH16 initialized
// RELOC-NEXT: R_MMIX_TPREL_HI16 initialized

_Thread_local long initialized = 42;
__thread long zero;
extern _Thread_local long decl;
__thread long explicit_model __attribute__((tls_model("local-exec"))) = 7;

long access(long value) {
  zero = value;
  return initialized + decl + explicit_model;
}
long *local_address(void) {
  static _Thread_local long local;
  return &local;
}
void *read_tp(void) { return __builtin_thread_pointer(); }
