; REQUIRES: mmix
; RUN: split-file %s %t
; RUN: llc -mtriple=mmix-unknown-linux -verify-machineinstrs -filetype=obj %t/caller.ll -o %t/linux.o
; RUN: llc -mtriple=mmix-unknown-unknown -verify-machineinstrs -filetype=obj %t/caller.ll -o %t/generic.o
; RUN: llvm-objdump --no-print-imm-hex -dr %t/linux.o | FileCheck %s --check-prefix=OBJECT --implicit-check-not=R_MMIX_PUSHJ_STUBBABLE
; RUN: llvm-objdump --no-print-imm-hex -dr %t/generic.o | FileCheck %s --check-prefix=OBJECT --implicit-check-not=R_MMIX_PUSHJ_STUBBABLE
; RUN: ld.lld -m elf64mmix_linux -T %t/layout.lds %t/linux.o -o %t/linux
; RUN: ld.lld -m elf64mmix -T %t/layout.lds %t/generic.o -o %t/generic
; RUN: llvm-objdump --no-print-imm-hex -d %t/linux | FileCheck %s --check-prefix=LINK --implicit-check-not=__MMIX_call_stub
; RUN: llvm-objdump --no-print-imm-hex -d %t/generic | FileCheck %s --check-prefix=LINK --implicit-check-not=__MMIX_call_stub
; RUN: llvm-readobj -r %t/linux | FileCheck %s --check-prefix=RELOCS
; RUN: llvm-readobj -r %t/generic | FileCheck %s --check-prefix=RELOCS

; A single function exceeds short-call reach. The following small function
; shares its input section, so function size alone cannot choose short calls.
; The far target is also beyond JMP reach; address expansion must stay in its
; reserved slots without moving instructions or changing the return address.
; OBJECT-LABEL: <large_caller>:
; OBJECT: GETA [[REG:r[0-9]+]], 0
; OBJECT-NEXT: {{.*}} R_MMIX_GETA far_target
; OBJECT-NEXT: {{.*}} SWYM
; OBJECT-NEXT: {{.*}} SWYM
; OBJECT-NEXT: {{.*}} SWYM
; OBJECT-NEXT: {{.*}} PUSHGO r31, [[REG]], 0
; OBJECT: PUSHGO r31,
; OBJECT-LABEL: <small_caller>:
; OBJECT: R_MMIX_GETA far_target
; OBJECT: PUSHGO r31,

; LINK-LABEL: <large_caller>:
; LINK: SETL [[REG:r[0-9]+]], 30600
; LINK-NEXT: {{.*}} INCML [[REG]], 21862
; LINK-NEXT: {{.*}} INCMH [[REG]], 13124
; LINK-NEXT: {{.*}} INCH [[REG]], 4386
; LINK-NEXT: {{.*}} PUSHGO r31, [[REG]], 0
; LINK: PUSHGO r31,
; LINK: POP
; LINK-LABEL: <small_caller>:
; LINK: PUSHGO r31,
; LINK: POP
; RELOCS: Relocations [
; RELOCS-NEXT: ]

;--- caller.ll
declare void @far_target()
define void @large_caller() nounwind {
  call void @far_target()
  call void asm sideeffect ".space 0x50000", ""()
  call void @far_target()
  ret void
}
define void @small_caller() nounwind {
  call void @far_target()
  ret void
}

;--- layout.lds
ENTRY(large_caller)
far_target = 0x1122334455667788;
SECTIONS { .text 0x10000 : { *(.text*) } }
