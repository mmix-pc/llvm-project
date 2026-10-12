; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs %s -o %t.o0.s
; RUN: FileCheck %s < %t.o0.s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj -r %t.o0.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA --implicit-check-not=R_MMIX_GOT
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -function-sections -verify-machineinstrs %s -o %t.o2.s
; RUN: FileCheck %s < %t.o2.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.o2.s -o %t.asm.o
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -function-sections -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA --implicit-check-not=R_MMIX_GOT
; RUN: llvm-readobj -r %t.asm.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA --implicit-check-not=R_MMIX_GOT

@local = internal global i64 1
@hidden = external hidden global i64
@protected = protected global i64 2
@alias = internal alias i64, ptr @local

; The GETA refers only to its own bounded literal, never to the distant object.
; CHECK-LABEL: local_address:
; CHECK: GETA [[BASE:r[0-9]+]], [[LIT:.Lpic_literal[0-9]+]]
; CHECK-NEXT: JMP [[END:.Lpic_end[0-9]+]]
; CHECK-NEXT: .p2align 3
; CHECK-NEXT: [[LIT]]:
; CHECK-NEXT: .8byte local-[[LIT]]
; CHECK-NEXT: [[END]]:
; CHECK: LDOU [[DELTA:r[0-9]+]], [[BASE]], 0
; CHECK: ADDU r231, [[BASE]], [[DELTA]]
; REL: R_MMIX_PC_64 .data 0x0
define ptr @local_address() { ret ptr @local }

; CHECK-LABEL: hidden_address:
; CHECK: .8byte hidden-.Lpic_literal
; REL: R_MMIX_PC_64 hidden 0x0
define ptr @hidden_address() { ret ptr @hidden }

; CHECK-LABEL: protected_address:
; CHECK: .8byte protected-.Lpic_literal
; REL: R_MMIX_PC_64 protected 0x0
define ptr @protected_address() { ret ptr @protected }

; CHECK-LABEL: alias_address:
; CHECK: .8byte {{(alias|local)}}-.Lpic_literal
define ptr @alias_address() { ret ptr @alias }

; CHECK-LABEL: function_address:
; CHECK: .8byte local_function-.Lpic_literal
define ptr @function_address() { ret ptr @local_function }
define internal void @local_function() noinline { ret void }

; Exercise same-section block-address deltas and a literal after a large span.
; CHECK-LABEL: block_address:
; CHECK: .8byte .Ltmp{{[0-9]*}}-.Lpic_literal
define ptr @block_address(i1 %cond) {
entry:
  br i1 %cond, label %first, label %second
first:
  call void asm sideeffect ".space 270000", ""()
  ret ptr blockaddress(@block_address, %second)
second:
  ret ptr blockaddress(@block_address, %first)
}
