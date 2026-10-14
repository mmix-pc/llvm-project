; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic %s -o %t.s
; RUN: FileCheck %s --check-prefix=ASM < %t.s
; RUN: llc -O0 -mtriple=mmix-unknown-linux -relocation-model=pic -filetype=obj %s -o %t.o
; RUN: llvm-mc -triple=mmix-unknown-linux -position-independent -filetype=obj %t.s -o %t.mc.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ
; RUN: llvm-readobj -r %t.mc.o | FileCheck %s --check-prefix=OBJ
; RUN: llvm-dwarfdump --eh-frame %t.o | FileCheck %s --check-prefix=CFI
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=static %s -o - | FileCheck %s --check-prefix=STATIC

declare i32 @__gcc_personality_v0(...)
declare void @callee()
define void @cleanup() personality ptr @__gcc_personality_v0 {
  invoke void @callee() to label %done unwind label %unwind
done:
  ret void
unwind:
  %lp = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %lp
}

; ASM: .cfi_personality 156, DW.ref.__gcc_personality_v0
; ASM: .cfi_lsda 28,
; ASM: .section .data.DW.ref.__gcc_personality_v0,"awG"
; ASM: .8byte __gcc_personality_v0
; OBJ: .rela.data.DW.ref.__gcc_personality_v0 {
; OBJ: R_MMIX_64 __gcc_personality_v0
; OBJ: .rela.eh_frame {
; OBJ-NOT: R_MMIX_64
; OBJ: R_MMIX_PC_64 DW.ref.__gcc_personality_v0
; OBJ: R_MMIX_PC_64 .text
; OBJ: R_MMIX_PC_64 .gcc_except_table
; OBJ-NOT: R_MMIX_64
; CFI: Augmentation: "zPLR"
; CFI: Augmentation data: 9C
; STATIC: .cfi_personality 0, __gcc_personality_v0
; STATIC: .cfi_lsda 0,
