; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.asm.o
; RUN: llvm-readobj -r %t.asm.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj -r %t.o0.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs -filetype=obj %s -o %t.o2.o
; RUN: llvm-readobj -r %t.o2.o | FileCheck %s --check-prefix=REL

@first = hidden thread_local(localdynamic) global [4 x i64] [i64 1, i64 2, i64 3, i64 4], align 32
@second = hidden thread_local(localdynamic) global i64 9, align 8

; The resolver yields the module base; a separate literal holds the symbol's
; module-local offset, never a PC-relative address or a second GOT descriptor.
; CHECK-LABEL: address:
; CHECK: .mmix_tlsld_pc64 first
; CHECK: .mmix_got_pc64 __tls_get_addr
; CHECK: PUSHGO
; CHECK: .mmix_dtpoff64 first
; CHECK: LDOU
; CHECK: ADDU
; CHECK: POP
define ptr @address() { ret ptr @first }

; CHECK-LABEL: offset:
; CHECK: .mmix_tlsld_pc64 second
; CHECK: PUSHGO
; CHECK: .mmix_dtpoff64 second
; CHECK: ADDU
; CHECK: POP
define ptr @offset() { ret ptr getelementptr(i8, ptr @second, i64 5) }

; CHECK-LABEL: two_objects:
; CHECK-DAG: .mmix_tlsld_pc64 first
; CHECK-DAG: .mmix_dtpoff64 first
; CHECK-DAG: .mmix_tlsld_pc64 second
; CHECK-DAG: .mmix_dtpoff64 second
; CHECK: POP
define i64 @two_objects() {
  %a = load i64, ptr getelementptr([4 x i64], ptr @first, i64 0, i64 2), align 8
  %b = load i64, ptr @second, align 8
  %sum = add i64 %a, %b
  ret i64 %sum
}

; REL: R_MMIX_TLSLD_PC64 first 0x0
; REL: R_MMIX_GOT_PC64 __tls_get_addr 0x0
; REL: R_MMIX_DTPOFF64 first 0x0
; REL: R_MMIX_TLSLD_PC64 second 0x0
; REL: R_MMIX_GOT_PC64 __tls_get_addr 0x0
; REL: R_MMIX_DTPOFF64 second 0x0
