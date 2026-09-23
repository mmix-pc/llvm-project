; REQUIRES: mmix

; RUN: rm -rf %t && split-file %s %t
; RUN: llvm-as %t/entry.ll -o %t/entry.bc
; RUN: llvm-as %t/compute.ll -o %t/compute.bc
; RUN: llc -mtriple=mmix-unknown-unknown -filetype=obj %t/entry.ll -o %t/entry.o
; RUN: llc -mtriple=mmix-unknown-unknown -filetype=obj %t/compute.ll -o %t/compute.o
; RUN: ld.lld -m elf64mmix -T %t/layout.lds -u compute -u observation \
; RUN:   %t/entry.bc %t/compute.bc -o %t/lto
; RUN: ld.lld -m elf64mmix -T %t/layout.lds -u compute -u observation \
; RUN:   %t/entry.o %t/compute.o -o %t/native
; RUN: llvm-readobj --file-headers --program-headers --sections --symbols \
; RUN:   --relocations %t/lto | FileCheck %s --check-prefix=STRUCTURE \
; RUN:   --implicit-check-not=.MMIX.reg_contents
; RUN: llvm-readobj --file-headers --program-headers --sections --symbols \
; RUN:   --relocations %t/native | FileCheck %s --check-prefix=STRUCTURE \
; RUN:   --implicit-check-not=.MMIX.reg_contents
; RUN: llvm-objdump --no-print-imm-hex -d %t/lto \
; RUN:   | FileCheck %s --check-prefix=DISASSEMBLY
; RUN: llvm-objdump --no-print-imm-hex -d %t/native \
; RUN:   | FileCheck %s --check-prefix=DISASSEMBLY
; RUN: llvm-objdump -s --section=.data %t/lto \
; RUN:   | FileCheck %s --check-prefix=DATA
; RUN: llvm-objdump -s --section=.data %t/native \
; RUN:   | FileCheck %s --check-prefix=DATA

; STRUCTURE:      Format: elf64-mmix
; STRUCTURE:      Arch: mmix
; STRUCTURE:      Type: Executable
; STRUCTURE:      Machine: EM_MMIX
; STRUCTURE:      Entry: 0x1000
; STRUCTURE:      Name: .text
; STRUCTURE:      Address: 0x1000
; STRUCTURE:      Name: .data
; STRUCTURE:      Address: 0x2000
; STRUCTURE:      Type: PT_LOAD
; STRUCTURE:      VirtualAddress: 0x1000
; STRUCTURE:      PF_X
; STRUCTURE:      Type: PT_LOAD
; STRUCTURE:      VirtualAddress: 0x2000
; STRUCTURE:      PF_W
; STRUCTURE:      Relocations [
; STRUCTURE-NEXT: ]
; STRUCTURE-DAG:  Name: _start
; STRUCTURE-DAG:  Name: compute
; STRUCTURE-DAG:  Name: input
; STRUCTURE-DAG:  Name: observation

; DISASSEMBLY-LABEL: <_start>:
; DISASSEMBLY:       PUSHGO
; DISASSEMBLY:       STO
; DISASSEMBLY:       JMP 0
; DISASSEMBLY-LABEL: <compute>:
; DISASSEMBLY:       LDO
; DISASSEMBLY:       ADDU
; DISASSEMBLY:       POP

; DATA:      Contents of section .data:
; DATA-NEXT: 2000 00000000 00000029 00000000 00000000

;--- entry.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-unknown"

@observation = external global i64

declare i64 @compute()

define void @_start() noreturn nounwind section ".text.start" {
entry:
  %value = call i64 @compute()
  store volatile i64 %value, ptr @observation, align 8
  br label %loop

loop:
  br label %loop
}

;--- compute.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-unknown"

@input = global i64 41, section ".data.input", align 8
@observation = global i64 0, section ".data.observation", align 8

define i64 @compute() noinline nounwind section ".text.compute" {
entry:
  %value = load volatile i64, ptr @input, align 8
  %result = add i64 %value, 1
  ret i64 %result
}

;--- layout.lds
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}
SECTIONS {
  .text 0x1000 : { *(.text.start) *(.text.compute) *(.text*) } :text
  .data 0x2000 : { *(.data.input) *(.data.observation) *(.data*) } :data
  /DISCARD/ : { *(.comment) }
}
