; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj -r %t.o0.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.asm.o
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA
; RUN: llvm-readobj -r %t.asm.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA

@external = external global i8
@defined = global i8 1
@weak = extern_weak global i8
@weak_defined = weak global i8 2
declare void @external_function()

; CHECK-LABEL: external_address:
; CHECK: GETA [[BASE:r[0-9]+]], [[LIT:.Lpic_literal[0-9]+]]
; CHECK: .mmix_got_pc64 external
; CHECK: LDOU [[DELTA:r[0-9]+]], [[BASE]], 0
; CHECK: LDOU r231, [[BASE]], [[DELTA]]
; REL: R_MMIX_GOT_PC64 external 0x0
define ptr @external_address() { ret ptr @external }

; CHECK-LABEL: offset_address:
; CHECK: .mmix_got_pc64 external
; CHECK: LDOU
; CHECK: LDOU
; CHECK: {{(ADDU|SUBU)}} r231,
; REL: R_MMIX_GOT_PC64 external 0x0
define ptr @offset_address() { ret ptr getelementptr(i8, ptr @external, i64 -9) }

; CHECK-LABEL: defined_address:
; CHECK: .mmix_got_pc64 defined
; REL: R_MMIX_GOT_PC64 defined 0x0
define ptr @defined_address() { ret ptr @defined }

; A missing weak symbol is represented by the slot's value, not its address.
; CHECK-LABEL: weak_address:
; CHECK: .mmix_got_pc64 weak
; CHECK: LDOU
; CHECK: LDOU r231,
; REL: R_MMIX_GOT_PC64 weak 0x0
define ptr @weak_address() { ret ptr @weak }

; CHECK-LABEL: weak_definition:
; CHECK: .mmix_got_pc64 weak_defined
; REL: R_MMIX_GOT_PC64 weak_defined 0x0
define ptr @weak_definition() { ret ptr @weak_defined }

; CHECK-LABEL: function_address:
; CHECK: .mmix_got_pc64 external_function
; REL: R_MMIX_GOT_PC64 external_function 0x0
define ptr @function_address() { ret ptr @external_function }

; CHECK-LABEL: defined_function_address:
; CHECK: .mmix_got_pc64 defined_function
; REL: R_MMIX_GOT_PC64 defined_function 0x0
define ptr @defined_function_address() { ret ptr @defined_function }
define void @defined_function() { ret void }

@public_alias = alias i8, ptr @defined
; CHECK-LABEL: alias_address:
; CHECK: .mmix_got_pc64 public_alias
; REL: R_MMIX_GOT_PC64 public_alias 0x0
define ptr @alias_address() { ret ptr @public_alias }

; Hidden undefined weak addresses must still be loaded from a nullable GOT slot.
@hidden_weak = extern_weak hidden global i8
declare extern_weak hidden void @hidden_weak_function()

; CHECK-LABEL: hidden_weak_address:
; CHECK: .mmix_got_pc64 hidden_weak
; REL: R_MMIX_GOT_PC64 hidden_weak 0x0
define ptr @hidden_weak_address() { ret ptr @hidden_weak }

; CHECK-LABEL: hidden_weak_function_address:
; CHECK: .mmix_got_pc64 hidden_weak_function
; REL: R_MMIX_GOT_PC64 hidden_weak_function 0x0
define ptr @hidden_weak_function_address() { ret ptr @hidden_weak_function }
