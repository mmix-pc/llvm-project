# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %s -o %t.linux.o
# RUN: llvm-readobj --sections --relocations %t.linux.o | FileCheck %s --implicit-check-not=.MMIX.reg_contents --implicit-check-not=R_MMIX_BASE_PLUS_OFFSET
# RUN: llvm-mc -triple=mmix-unknown-unknown -filetype=obj %s -o %t.generic.o
# RUN: llvm-readobj --sections --relocations %t.generic.o | FileCheck %s --implicit-check-not=.MMIX.reg_contents --implicit-check-not=R_MMIX_BASE_PLUS_OFFSET

# Explicit GETA addends use the entire signed ELF64 RELA field, independently
# of whether CodeGen folds a particular source expression into a relocation.
# Each expanding instruction reserves sixteen bytes. Final address validity
# and selection of direct versus expanded instructions belong to the linker.
# CHECK: Name: .text
# CHECK: Size: 64
# CHECK: 0x0 R_MMIX_GETA external 0x1
# CHECK-NEXT: 0x10 R_MMIX_GETA external 0xFFFFFFFFFFFFFFFF
# CHECK-NEXT: 0x20 R_MMIX_GETA external 0x7FFFFFFFFFFFFFFF
# CHECK-NEXT: 0x30 R_MMIX_GETA external 0x8000000000000000
GETA r0, %geta(external + 1)
GETA r1, %geta(external - 1)
GETA r2, %geta(external + 9223372036854775807)
GETA r3, %geta(external - 9223372036854775808)
