; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj --sections --relocations %t.o0.o | FileCheck %s --check-prefix=RELOC --implicit-check-not=.MMIX.reg_contents --implicit-check-not=R_MMIX_BASE_PLUS_OFFSET
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o0.o | FileCheck %s --check-prefix=CODE
; RUN: llc -mtriple=mmix-unknown-linux-unknown -O2 -verify-machineinstrs -filetype=obj %s -o %t.o2.o
; RUN: llvm-readobj --sections --relocations %t.o2.o | FileCheck %s --check-prefix=RELOC --implicit-check-not=.MMIX.reg_contents --implicit-check-not=R_MMIX_BASE_PLUS_OFFSET
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o2.o | FileCheck %s --check-prefix=CODE
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs -filetype=obj %s -o %t.generic.o
; RUN: llvm-readobj --sections --relocations %t.generic.o | FileCheck %s --check-prefix=RELOC --implicit-check-not=.MMIX.reg_contents --implicit-check-not=R_MMIX_BASE_PLUS_OFFSET
; RUN: llc -mtriple=mmix-unknown-linux -O2 -filetype=asm %s -o %t.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-readobj --sections --relocations %t.roundtrip.o | FileCheck %s --check-prefix=RELOC --implicit-check-not=.MMIX.reg_contents --implicit-check-not=R_MMIX_BASE_PLUS_OFFSET
; RUN: llvm-objdump --no-print-imm-hex -dr %t.roundtrip.o | FileCheck %s --check-prefix=CODE

; These non-inbounds GEPs use wrapping runtime arithmetic after materializing
; the base. They need neither a GREG base nor folded relocation addends.
; RELOC: R_MMIX_GETA external_data 0x0
; RELOC: R_MMIX_GETA external_data 0x0
; RELOC: R_MMIX_GETA external_data 0x0
; RELOC: R_MMIX_GETA external_data 0x0
; RELOC: R_MMIX_GETA external_function 0x0
; RELOC: R_MMIX_GETA .data 0x0

@external_data = external global i8
@local_data = internal global i8 1
declare void @external_function()

; Each address uses a destination register plus three reserved instructions,
; never a loader-initialized global base. Do not fix the allocated register.
; CODE-LABEL: <odd_address>:
; CODE: GETA [[REG:r[0-9]+]], 0
; CODE-NEXT: {{.*}} R_MMIX_GETA external_data
; CODE-NEXT: {{.*}} SWYM 0, 0, 0
; CODE-NEXT: {{.*}} SWYM 0, 0, 0
; CODE-NEXT: {{.*}} SWYM 0, 0, 0
; CODE: ADDU r231, [[REG]], 1
define ptr @odd_address() {
  ret ptr getelementptr (i8, ptr @external_data, i64 1)
}

; CODE-LABEL: <negative_address>:
; CODE: GETA [[NEG:r[0-9]+]], 0
; CODE: SUBU r231, [[NEG]], 1
define ptr @negative_address() {
  ret ptr getelementptr (i8, ptr @external_data, i64 -1)
}

; CODE-LABEL: <maximum_addend>:
; CODE: SETH [[MAX:r[0-9]+]], 32768
; CODE-NEXT: {{.*}} NOR [[MAX]], [[MAX]], 0
; CODE: GETA [[MAXBASE:r[0-9]+]], 0
; CODE: ADDU r231, [[MAXBASE]], [[MAX]]
define ptr @maximum_addend() {
  ret ptr getelementptr (i8, ptr @external_data, i64 9223372036854775807)
}

; CODE-LABEL: <minimum_addend>:
; CODE: SETH [[MIN:r[0-9]+]], 32768
; CODE: GETA [[MINBASE:r[0-9]+]], 0
; CODE: ADDU r231, [[MINBASE]], [[MIN]]
define ptr @minimum_addend() {
  ret ptr getelementptr (i8, ptr @external_data, i64 -9223372036854775808)
}

; CODE-LABEL: <function_address>:
; CODE: GETA {{r[0-9]+}}, 0
define ptr @function_address() {
  ret ptr @external_function
}

; CODE-LABEL: <local_address>:
; CODE: GETA {{r[0-9]+}}, 0
define ptr @local_address() {
  ret ptr @local_data
}
