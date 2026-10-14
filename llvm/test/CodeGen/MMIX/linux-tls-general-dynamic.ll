; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.asm.o
; RUN: llvm-readobj -r %t.asm.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj -r %t.o0.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs -filetype=obj %s -o %t.o2.o
; RUN: llvm-readobj -r %t.o2.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=CALL

@first = external thread_local global i64
@second = thread_local global i64 9
declare void @consume(ptr, ptr, i64)

; CHECK-LABEL: address:
; CHECK: .mmix_tlsgd_pc64 first
; CHECK: .mmix_got_pc64 __tls_get_addr
; CHECK: PUSHGO
; CHECK: POP
define ptr @address() { ret ptr @first }

; CALL-LABEL: name: address
; CALL: hasCalls: true
; CALL: TLS_GD_LITERAL @first
; CALL: GOT_LITERAL &__tls_get_addr
; CALL: $r231 = COPY
; CALL: CALL_STATE {{.*}}csr_mmix_linux{{.*}}implicit $r231{{.*}}implicit-def $r231
; CALL: COPY $r231

; CHECK-LABEL: offset:
; CHECK: .mmix_tlsgd_pc64 first
; CHECK: PUSHGO
; CHECK: {{(ADDU|SUBU)}}
; CHECK: POP
define ptr @offset() { ret ptr getelementptr(i8, ptr @first, i64 -9) }

; Two resolver calls and a normal call must preserve live values and windows.
; CHECK-LABEL: preserve:
; CHECK-DAG: .mmix_tlsgd_pc64 first
; CHECK-DAG: .mmix_tlsgd_pc64 second
; CHECK: .mmix_got_pc64 consume
; CHECK: PUSHGO
; CHECK: POP
define i64 @preserve(i64 %value) {
  %old = load i64, ptr @first, align 8
  call void @consume(ptr @first, ptr @second, i64 %value)
  %sum = add i64 %value, %old
  ret i64 %sum
}

; REL: R_MMIX_TLSGD_PC64 first 0x0
; REL: R_MMIX_GOT_PC64 __tls_get_addr 0x0
; REL: R_MMIX_TLSGD_PC64 first 0x0
; REL: R_MMIX_GOT_PC64 __tls_get_addr 0x0
; REL-DAG: R_MMIX_TLSGD_PC64 first 0x0
; REL-DAG: R_MMIX_TLSGD_PC64 second 0x0
; REL: R_MMIX_GOT_PC64 consume 0x0
