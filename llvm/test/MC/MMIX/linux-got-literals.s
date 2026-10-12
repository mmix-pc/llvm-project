# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %s -o %t.o
# RUN: llvm-readobj --relocations --symbols --sections --section-data %t.o | FileCheck %s
# RUN: llvm-readobj --sections --section-data %t.o | FileCheck %s --check-prefix=PC
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=asm %s -o %t.s
# RUN: FileCheck %s --check-prefix=ASM < %t.s
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.roundtrip.o
# RUN: cmp %t.o %t.roundtrip.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=null %s

# GOT references retain the actual symbol, including temporary local definitions
# in the literal's own section, instead of folding to its current address.
# CHECK: Name: .rodata
# CHECK: Size: 80
# CHECK: AddressAlignment: 8
# CHECK: 0000: 00000000 00000000 00000000 00000000
# CHECK-NEXT: 0010: 00000000 00000000 00000000 00000000
# CHECK-NEXT: 0020: 00000000 00000000 00000000 00000000
# CHECK-NEXT: 0030: 00000000 00000000 00000000 00000000
# CHECK-NEXT: 0040: 00000000 00000000 00000000 00000000
# CHECK: 0x8 R_MMIX_GOT_PC64 external 0x0
# CHECK-NEXT: 0x10 R_MMIX_GOT_PC64 external 0x8
# CHECK-NEXT: 0x18 R_MMIX_GOT_PC64 .Lbefore 0xFFFFFFFFFFFFFFF8
# CHECK-NEXT: 0x20 R_MMIX_GOT_PC64 .Lafter 0x0
# CHECK-NEXT: 0x28 R_MMIX_GOT_PC64 weak_object 0x0
# CHECK-NEXT: 0x30 R_MMIX_GOT_PC64 function 0x7FFFFFFFFFFFFFFF
# CHECK-NEXT: 0x38 R_MMIX_GOT_PC64 external 0x8000000000000000
# CHECK-NEXT: 0x40 R_MMIX_GOT_PC64 global_object 0x0
# CHECK: 0x0 R_MMIX_PC_64 {{(global_object|.data)}} 0x0
# CHECK: Name: .Lbefore
# CHECK: Name: .Lafter
# CHECK: Name: weak_object
# CHECK: Binding: Weak
# CHECK: Name: function
# CHECK: Type: Function
# ASM: .mmix_got_pc64 external
# ASM: .mmix_got_pc64 external+8
# ASM: .mmix_got_pc64 .Lbefore-8

.section .rodata,"a",@progbits
.Lbefore:
.8byte 0
.mmix_got_pc64 external
.mmix_got_pc64 external + 8
.mmix_got_pc64 .Lbefore - 8
.mmix_got_pc64 .Lafter
.mmix_got_pc64 weak_object
.mmix_got_pc64 function + 9223372036854775807
.mmix_got_pc64 external - 9223372036854775808
.mmix_got_pc64 global_object
.Lafter:
.8byte 0
.weak weak_object
.type function,@function
.globl global_object
.data
global_object:
.8byte 0

# Ordinary same-image address differences still use the generic data path.
.section .localdiff,"a",@progbits
.8byte global_object - .
.Lorigin:
.8byte .Lforward - .
.Lforward:
.8byte .Lorigin - .
# PC: Name: .localdiff
# PC: SectionData (
# PC-NEXT: 0000: 00000000 00000000 00000000 00000008
# PC-NEXT: 0010: FFFFFFFF FFFFFFF8
