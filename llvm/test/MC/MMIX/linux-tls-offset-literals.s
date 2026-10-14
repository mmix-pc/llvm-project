# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %s -o %t.o
# RUN: llvm-readobj -r --symbols %t.o | FileCheck %s
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=asm %s -o %t.s
# RUN: FileCheck %s --check-prefix=ASM < %t.s
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.roundtrip.o
# RUN: cmp %t.o %t.roundtrip.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=null %s

# This literal holds a module-relative offset, not a PC-relative displacement.
.section .rodata,"a",@progbits
.8byte 0
.mmix_dtpoff64 external
.mmix_dtpoff64 local + 8
.mmix_dtpoff64 local - 9223372036854775808
.mmix_dtpoff64 external + 9223372036854775807
# CHECK:      0x8 R_MMIX_DTPOFF64 external 0x0
# CHECK-NEXT: 0x10 R_MMIX_DTPOFF64 local 0x8
# CHECK-NEXT: 0x18 R_MMIX_DTPOFF64 local 0x8000000000000000
# CHECK-NEXT: 0x20 R_MMIX_DTPOFF64 external 0x7FFFFFFFFFFFFFFF
.section .tdata,"awT",@progbits
.space 16
local:
.8byte 42
.mmix_dtpoff64 local
# CHECK: 0x18 R_MMIX_DTPOFF64 local 0x0
# CHECK: Name: local
# CHECK: Value: 0x10
# CHECK: Type: TLS
# CHECK: Name: external
# CHECK: Type: TLS
# ASM: .mmix_dtpoff64 external
# ASM: .mmix_dtpoff64 local+8
# ASM: .mmix_dtpoff64 local-9223372036854775808
