; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj -r %t.o0.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA --implicit-check-not=R_MMIX_PUSHJ
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.asm.o
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA --implicit-check-not=R_MMIX_PUSHJ --implicit-check-not=R_MMIX_ADDR19
; RUN: llvm-readobj -r %t.asm.o | FileCheck %s --check-prefix=REL --implicit-check-not=R_MMIX_GETA --implicit-check-not=R_MMIX_PUSHJ --implicit-check-not=R_MMIX_ADDR19

declare i64 @external(i64)
declare fastcc i64 @fast(i64)
%result = type { i64, i64 }
declare void @sret_target(ptr sret(%result), i64)
declare i64 @vararg_target(i64, ...)

; CHECK-LABEL: normal:
; CHECK: GET {{r[0-9]+}}, rJ
; CHECK: .mmix_got_pc64 external
; CHECK: PUSHGO r31,
; CHECK: PUT rJ,
; CHECK: POP
; REL: R_MMIX_GOT_PC64 external 0x0
define i64 @normal(i64 %x) nonlazybind {
  %v = call i64 @external(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; CHECK-LABEL: local_call:
; CHECK: .8byte local-.Lpic_literal
; CHECK: PUSHGO r31,
; CHECK: POP
define i64 @local_call(i64 %x) {
  %v = call i64 @local(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}
define internal i64 @local(i64 %x) noinline section ".text.separate" { ret i64 %x }

; CHECK-LABEL: tail:
; CHECK: .mmix_got_pc64 external
; CHECK-NOT: PUSH
; CHECK: GO r255,
define i64 @tail(i64 %x) {
  %v = musttail call i64 @external(i64 %x)
  ret i64 %v
}

; CHECK-LABEL: fast_tail:
; CHECK: .mmix_got_pc64 fast
; CHECK-NOT: PUSH
; CHECK: GO r255,
define fastcc i64 @fast_tail(i64 %x) {
  %v = musttail call fastcc i64 @fast(i64 %x)
  ret i64 %v
}

; CHECK-LABEL: sret_tail:
; CHECK: .mmix_got_pc64 sret_target
; CHECK-NOT: PUSH
; CHECK: GO r255,
define void @sret_tail(ptr sret(%result) %out, i64 %x) {
  musttail call void @sret_target(ptr sret(%result) %out, i64 %x)
  ret void
}

; CHECK-LABEL: vararg_call:
; CHECK: .mmix_got_pc64 vararg_target
; CHECK: PUSHGO r31,
define i64 @vararg_call(i64 %x) {
  %v = call i64(i64, ...) @vararg_target(i64 %x, i64 17)
  ret i64 %v
}

; CHECK-LABEL: indirect_tail:
; CHECK-NOT: .mmix_got_pc64
; CHECK-NOT: PUSH
; CHECK: GO r255,
define i64 @indirect_tail(ptr %f, i64 %x) {
  %v = musttail call i64 %f(ptr %f, i64 %x)
  ret i64 %v
}

declare extern_weak hidden i64 @hidden_weak(i64)

; CHECK-LABEL: hidden_weak_call:
; CHECK: .mmix_got_pc64 hidden_weak
; CHECK: PUSHGO r31,
; REL: R_MMIX_GOT_PC64 hidden_weak 0x0
define i64 @hidden_weak_call(i64 %x) {
  %v = call i64 @hidden_weak(i64 %x)
  %r = add i64 %v, 1
  ret i64 %r
}

; CHECK-LABEL: hidden_weak_tail:
; CHECK: .mmix_got_pc64 hidden_weak
; CHECK: GO r255,
; REL: R_MMIX_GOT_PC64 hidden_weak 0x0
define i64 @hidden_weak_tail(i64 %x) {
  %v = musttail call i64 @hidden_weak(i64 %x)
  ret i64 %v
}
