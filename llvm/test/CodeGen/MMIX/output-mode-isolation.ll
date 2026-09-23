; RUN: llc -mtriple=mmix-unknown-elf -filetype=asm \
; RUN:   %s -o %t.s
; RUN: FileCheck %s --check-prefix=CANONICAL \
; RUN:   --implicit-check-not=PUSHJ < %t.s
; RUN: llc -mtriple=mmix-unknown-elf -filetype=asm \
; RUN:   --output-asm-variant=1 %s -o %t.mms
; RUN: FileCheck %s --check-prefix=MMIXAL --implicit-check-not=: \
; RUN:   --implicit-check-not=PUSHJ \
; RUN:   --implicit-check-not='{{^[[:space:]]*\.}}' < %t.mms
; RUN: llc -mtriple=mmix-unknown-elf -filetype=obj %s -o %t.o
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=ELF \
; RUN:     --implicit-check-not=SETH --implicit-check-not=PUSHGO
; RUN: not llc -mtriple=mmix-unknown-elf -filetype=obj \
; RUN:   --output-asm-variant=1 %s -o %t.mmixal.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MMIXAL-OBJECT
; RUN: test ! -s %t.mmixal.o

; MMIXAL-OBJECT: error: target does not support generation of this file type
; ELF-LABEL: <Main>:
; ELF:       GETA [[OBJECT_DATA:r[0-9]+]], 0
; ELF-NEXT:  {{.*}} R_MMIX_GETA named.data
; ELF:       GETA {{r[0-9]+}}, 0
; ELF-NEXT:  {{.*}} R_MMIX_GETA worker
; ELF:       PUSHGO r31, {{r[0-9]+}}, 0
; CANONICAL:      .text
; CANONICAL:      .globl Main
; CANONICAL:      .type Main,@function
; CANONICAL-LABEL: Main:
; CANONICAL:      GETA [[DATA:r[0-9]+]], %geta(named.data)
; CANONICAL:      GETA [[CALLEE:r[0-9]+]], %geta(worker)
; CANONICAL-NEXT: PUSHGO r31, [[CALLEE]], 0
; CANONICAL:      .globl worker
; CANONICAL-LABEL: worker:
; CANONICAL:      .data
; CANONICAL:      .globl named.data
; CANONICAL-LABEL: named.data:
; CANONICAL-NEXT: .8byte 7

; MMIXAL:      LOC #0000000000000134
; MMIXAL-NEXT: [[LOOP:__LLVM_L_F_4D61696E_BB_[0-9]+]]	IS @
; MMIXAL-NEXT: loop	IS @
; MMIXAL-NEXT: JMP [[LOOP]]
; MMIXAL:      LOC #2000000000000000
; MMIXAL-NEXT: [[DATA:__LLVM_U_10_6E616D65642E64617461]]	IS @
; MMIXAL-NEXT: BYTE 0,0,0,0,0,0,0,#07
; MMIXAL:      worker	IS @
; MMIXAL:      SETH [[WORK_DATA:\$[0-9]+]],[[DATA]]>>48&65535
; MMIXAL-NEXT: INCMH [[WORK_DATA]],[[DATA]]>>32&65535
; MMIXAL-NEXT: INCML [[WORK_DATA]],[[DATA]]>>16&65535
; MMIXAL-NEXT: INCL [[WORK_DATA]],[[DATA]]&65535
; MMIXAL:      LOC #0000000000000100
; MMIXAL-NEXT: Main	IS @
; MMIXAL-NEXT: PUT rA,0
; MMIXAL-NEXT: PUT rL,0
; MMIXAL:      SETH [[MAIN_DATA:\$[0-9]+]],[[DATA]]>>48&65535
; MMIXAL-NEXT: INCMH [[MAIN_DATA]],[[DATA]]>>32&65535
; MMIXAL-NEXT: INCML [[MAIN_DATA]],[[DATA]]>>16&65535
; MMIXAL-NEXT: INCL [[MAIN_DATA]],[[DATA]]&65535
; MMIXAL:      SETH [[CALLEE:\$[0-9]+]],worker>>48&65535
; MMIXAL-NEXT: INCMH [[CALLEE]],worker>>32&65535
; MMIXAL-NEXT: INCML [[CALLEE]],worker>>16&65535
; MMIXAL-NEXT: INCL [[CALLEE]],worker&65535
; MMIXAL-NEXT: PUSHGO $31,[[CALLEE]],0

target triple = "mmix-unknown-elf"

@"named.data" = global i64 7, align 8

define void @Main() {
entry:
  %value = load volatile i64, ptr @"named.data", align 8
  call void @worker(i64 %value)
  br label %loop

loop:
  br label %loop
}

define void @worker(i64 %value) noinline {
  store volatile i64 %value, ptr @"named.data", align 8
  ret void
}
