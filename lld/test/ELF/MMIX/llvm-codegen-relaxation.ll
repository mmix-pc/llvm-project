; REQUIRES: mmix

; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=mmix-unknown-elf -filetype=obj %t/producer.ll \
; RUN:   -o %t/producer.o
; RUN: llvm-mc -triple=mmix -filetype=obj %t/definitions.s \
; RUN:   -o %t/definitions.o
; RUN: ld.lld -T %t/near.lds %t/producer.o %t/definitions.o -o %t/near
; RUN: ld.lld -T %t/far.lds %t/producer.o %t/definitions.o -o %t/far
; RUN: llvm-readobj --file-headers --sections --program-headers --symbols \
; RUN:   --relocations %t/near | FileCheck %s --check-prefix=NEAR
; RUN: llvm-readobj --file-headers --sections --program-headers --symbols \
; RUN:   --relocations %t/far | FileCheck %s --check-prefix=FAR
; RUN: llvm-objdump --no-print-imm-hex -d %t/near \
; RUN:   | FileCheck %s --check-prefix=NEAR-DIS
; RUN: llvm-objdump --no-print-imm-hex -d %t/far \
; RUN:   | FileCheck %s --check-prefix=FAR-DIS
; RUN: llvm-objdump -s --section=.data.pointer %t/far \
; RUN:   | FileCheck %s --check-prefix=DATA

; NEAR:      Format: elf64-mmix
; NEAR:      Type: Executable
; NEAR:      Name: .text.entry
; NEAR:      Name: .text.defined
; NEAR:      Name: .text.external
; NEAR:      Name: .data
; NEAR-NOT:  Name: .MMIX.reg_contents
; NEAR:      Type: PT_LOAD
; NEAR:      Relocations [
; NEAR-NEXT: ]
; NEAR-NOT:  Name: __MMIX_call_stub_

; FAR:      Format: elf64-mmix
; FAR:      Name: .text.entry
; FAR:      Name: .text.defined
; FAR:      Name: .text.external
; FAR:      Name: .data.defined
; FAR:      Name: .data.external
; FAR:      Name: .data.pointer
; FAR-NOT:  Name: .MMIX.reg_contents
; FAR:      Type: PT_LOAD
; FAR:      Relocations [
; FAR-NEXT: ]
; FAR-NOT:  Name: __MMIX_call_stub_

; NEAR-DIS-LABEL: <defined_data_address>:
; NEAR-DIS:       GETA r231, {{[0-9]+}}
; NEAR-DIS-LABEL: <external_data_address>:
; NEAR-DIS:       GETA r231, {{[0-9]+}}
; NEAR-DIS-LABEL: <defined_function_address>:
; NEAR-DIS:       GETA r231, {{[0-9]+}}
; NEAR-DIS-LABEL: <external_function_address>:
; NEAR-DIS:       GETA r231, {{[0-9]+}}
; NEAR-DIS-LABEL: <call_defined>:
; NEAR-DIS:       GETA [[NEAR_REG:r[0-9]+]], {{[0-9]+}}
; NEAR-DIS:       PUSHGO r31, [[NEAR_REG]], 0
; NEAR-DIS-LABEL: <call_external>:
; NEAR-DIS:       GETA [[NEAR_EXT:r[0-9]+]], {{[0-9]+}}
; NEAR-DIS:       PUSHGO r31, [[NEAR_EXT]], 0

; FAR-DIS-LABEL: <defined_data_address>:
; FAR-DIS:       GETA r231, {{[0-9]+}}
; FAR-DIS-LABEL: <external_data_address>:
; FAR-DIS:       SETL r231,
; FAR-DIS:       INCML r231,
; FAR-DIS-LABEL: <defined_function_address>:
; FAR-DIS:       GETA r231, {{[0-9]+}}
; FAR-DIS-LABEL: <external_function_address>:
; FAR-DIS:       SETL r231,
; FAR-DIS:       INCML r231,
; FAR-DIS-LABEL: <call_defined>:
; FAR-DIS:       GETA [[FAR_REG:r[0-9]+]], {{[0-9]+}}
; FAR-DIS:       PUSHGO r31, [[FAR_REG]], 0
; FAR-DIS-LABEL: <call_external>:
; FAR-DIS:       SETL [[FAR_EXT:r[0-9]+]],
; FAR-DIS:       INCMH [[FAR_EXT]],
; FAR-DIS:       PUSHGO r31, [[FAR_EXT]], 0

; DATA:      Contents of section .data.pointer:
; DATA-NEXT: 210000 00000000 00200000

;--- producer.ll
target triple = "mmix-unknown-elf"

@defined_data = global i64 7, section ".data.defined", align 8
@external_data = external global i64

declare void @external_function()

define void @defined_function() section ".text.defined" {
  ret void
}

define ptr @defined_data_address() section ".text.entry" {
  ret ptr @defined_data
}

define ptr @external_data_address() section ".text.entry" {
  ret ptr @external_data
}

define ptr @defined_function_address() section ".text.entry" {
  ret ptr @defined_function
}

define ptr @external_function_address() section ".text.entry" {
  ret ptr @external_function
}

define void @call_defined() section ".text.entry" {
  call void @defined_function()
  ret void
}

define void @call_external() section ".text.entry" {
  call void @external_function()
  ret void
}

@external_pointer = global ptr @external_data, section ".data.pointer", align 8

;--- definitions.s
.section .text.external,"ax",@progbits
.global external_function
.type external_function,@function
external_function:
  POP 0, 0
.section .data.external,"aw",@progbits
.global external_data
.type external_data,@object
external_data:
  .quad 0x1122334455667788

;--- near.lds
ENTRY(defined_data_address)
SECTIONS {
  .text.entry 0x1000 : { *(.text.entry) }
  .text.defined 0x2000 : { *(.text.defined) }
  .text.external 0x3000 : { *(.text.external) }
  .data 0x4000 : { *(.data.*) }
}

;--- far.lds
ENTRY(defined_data_address)
SECTIONS {
  .text.entry 0x1000 : { *(.text.entry) }
  .text.defined 0x2000 : { *(.text.defined) }
  .text.external 0x100000000 : { *(.text.external) }
  .data.defined 0x3000 : { *(.data.defined) }
  .data.external 0x200000 : { *(.data.external) }
  .data.pointer 0x210000 : { *(.data.pointer) }
}
