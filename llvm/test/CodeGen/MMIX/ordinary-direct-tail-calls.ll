; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %s -o - \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=RELOC
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=OBJ

declare void @side_effect()
declare i64 @unresolved(i64)
declare i64 @seventeen(i64, i64, i64, i64, i64, i64, i64, i64, i64,
                       i64, i64, i64, i64, i64, i64, i64, i64)
%pair = type { i64, i64 }
declare void @sret_target(ptr sret(%pair), i64)
declare void @byval_target(ptr byval(%pair))

define internal i64 @local_target(i64 %value) {
  ret i64 %value
}

define internal i64 @section_target(i64 %value) section ".text.tail.target" {
  ret i64 %value
}

; ASM-LABEL: leaf_direct:
; ASM-NOT: PUSH
; ASM: JMP{{B?}} local_target
define i64 @leaf_direct(i64 %value) {
  %result = tail call i64 @local_target(i64 %value)
  ret i64 %result
}

; ASM-LABEL: nonleaf_direct:
; ASM: GET r30, rJ
; ASM: PUSHGO
; ASM: PUT rJ, r30
; ASM-NOT: PUSHJ
; ASM: JMP{{B?}} local_target
define i64 @nonleaf_direct(i64 %value) {
  call void @side_effect()
  %result = tail call i64 @local_target(i64 %value)
  ret i64 %result
}

; ASM-LABEL: framed_direct:
; ASM: SUBU r254, r254, 8
; ASM: ADDU r254, r254, 8
; ASM: JMP{{B?}} local_target
define i64 @framed_direct(i64 %value) {
  %slot = alloca i64, align 8
  store volatile i64 %value, ptr %slot, align 8
  %result = tail call i64 @local_target(i64 %value)
  ret i64 %result
}

; ASM-LABEL: recursive_direct:
; ASM-NOT: PUSH
; ASM: JMPB recursive_direct
define i64 @recursive_direct(i64 %value) {
  %result = tail call i64 @recursive_direct(i64 %value)
  ret i64 %result
}

; ASM-LABEL: cross_section_direct:
; ASM-NOT: PUSH
; ASM: GO r255
define i64 @cross_section_direct(i64 %value) {
  %result = tail call i64 @section_target(i64 %value)
  ret i64 %result
}

; ASM-LABEL: unresolved_direct:
; ASM-NOT: PUSH
; ASM: GO r255
define i64 @unresolved_direct(i64 %value) {
  %result = tail call i64 @unresolved(i64 %value)
  ret i64 %result
}

; ASM-LABEL: forwarded_sret_direct:
; ASM-NOT: PUSH
; ASM: GO r255
define void @forwarded_sret_direct(ptr sret(%pair) %result, i64 %value) {
  tail call void @sret_target(ptr sret(%pair) %result, i64 %value)
  ret void
}

; A caller-owned aggregate copy cannot survive destruction of this frame.
; ASM-LABEL: caller_copy_fallback_direct:
; ASM: PUSH{{J|GO}}
; ASM: POP 0, 0
define void @caller_copy_fallback_direct(ptr %value) {
  tail call void @byval_target(ptr byval(%pair) %value)
  ret void
}

; The outgoing stack slot cannot be reused by this zero-stack-argument caller.
; ASM-LABEL: stack_fallback_direct:
; ASM: PUSH{{J|GO}}
; ASM: POP 0, 0
define i64 @stack_fallback_direct() {
  %result = tail call i64 @seventeen(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8,
      i64 9, i64 10, i64 11, i64 12, i64 13, i64 14, i64 15, i64 16)
  ret i64 %result
}

; RELOC: Type: R_MMIX_GETA
; RELOC-NEXT: Symbol: side_effect
; RELOC: Type: R_MMIX_GETA
; RELOC-NEXT: Symbol: .text.tail.target
; RELOC: Type: R_MMIX_GETA
; RELOC-NEXT: Symbol: unresolved
; RELOC: Type: R_MMIX_GETA
; RELOC-NEXT: Symbol: sret_target

; OBJ-LABEL: <leaf_direct>:
; OBJ: JMP
; OBJ-LABEL: <recursive_direct>:
; OBJ: JMPB
; OBJ-LABEL: <cross_section_direct>:
; OBJ: GO r255
; OBJ-LABEL: <unresolved_direct>:
; OBJ: GO r255
; OBJ-LABEL: <forwarded_sret_direct>:
; OBJ: GO r255
; OBJ-LABEL: <caller_copy_fallback_direct>:
; OBJ:       PUSHGO
; OBJ-LABEL: <stack_fallback_direct>:
; OBJ:       PUSHGO
