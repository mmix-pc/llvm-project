# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/main.s -o %t/main.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: ld.lld -m elf64mmix_linux --gc-sections %t/main.o %t/provider.o %t/provider.o -o %t/selected
# RUN: llvm-objdump -d %t/selected | FileCheck %s --check-prefix=BOUNDARY
# RUN: llvm-readobj --symbols %t/selected | FileCheck %s --check-prefix=SYMBOL --implicit-check-not=unused
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/dead.s -o %t/dead.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/start.s -o %t/start.o
# RUN: ld.lld -m elf64mmix_linux --gc-sections %t/start.o %t/dead.o -o %t/dead
# RUN: llvm-ar crs %t/dead.a %t/dead.o
# RUN: ld.lld -m elf64mmix_linux %t/start.o %t/dead.a -o %t/archive
# RUN: ld.lld -m elf64mmix_linux -r %t/start.o %t/main.o %t/provider.o --allow-multiple-definition -o %t/partial.o
# RUN: llvm-readobj --relocations %t/partial.o | FileCheck %s --check-prefix=PARTIAL
# RUN: not ld.lld -m elf64mmix_linux -T %t/discard.ld %t/main.o %t/provider.o -o %t/discard 2>&1 | FileCheck %s --check-prefix=DISCARD

## D=16, t=0. Exercise the signed maximum, negative values and the smallest
## representable addend. Each wyde independently checks the full value.
# BOUNDARY: e3 01 ff ff
# BOUNDARY: e6 02 ff ff
# BOUNDARY: e5 03 ff ff
# BOUNDARY: e4 04 7f ff
# BOUNDARY: e3 05 00 10
# BOUNDARY: e4 06 80 00
# BOUNDARY: e3 07 ff ff
# BOUNDARY: e3 08 cd ef
# BOUNDARY: e6 09 89 ab
# BOUNDARY: e5 0a 45 67
# BOUNDARY: e4 0b 01 23
# SYMBOL: Name: value
# SYMBOL: Binding: Weak
# SYMBOL-NEXT: Type: TLS
# PARTIAL: 0x4 R_MMIX_TPREL_LO16 value 0x7FFFFFFFFFFFFFEF
# PARTIAL: 0x10 R_MMIX_TPREL_HI16 value 0x7FFFFFFFFFFFFFEF
# DISCARD: requires a defined symbol in the executable TLS image

#--- main.s
.text
.globl _start
_start:
SETL r1, %tprel_lo(value + 9223372036854775791)
INCML r2, %tprel_ml(value + 9223372036854775791)
INCMH r3, %tprel_mh(value + 9223372036854775791)
INCH r4, %tprel_hi(value + 9223372036854775791)
SETL r5, %tprel_lo(value - 9223372036854775808)
INCH r6, %tprel_hi(value - 9223372036854775808)
SETL r7, %tprel_lo(value - 17)
SETL r8, %tprel_lo(value + 0x123456789abcddf)
INCML r9, %tprel_ml(value + 0x123456789abcddf)
INCMH r10, %tprel_mh(value + 0x123456789abcddf)
INCH r11, %tprel_hi(value + 0x123456789abcddf)

#--- provider.s
.section .tdata.value,"awTG",@progbits,value,comdat
.weak value
value: .8byte 1
.section .tdata.unused,"awT",@progbits
unused: .8byte 0

#--- dead.s
.section .text.dead,"ax",@progbits
SETL r1, %tprel_lo(missing)

#--- start.s
.text
.globl _start
_start: SWYM 0,0,0

#--- discard.ld
SECTIONS {
 .text 0x10000 : { *(.text) }
 /DISCARD/ : { *(.tdata*) }
}
