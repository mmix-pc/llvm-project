# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux -T %t/residue.ld %t/input.o -o %t/residue
# RUN: llvm-objdump -d %t/residue | FileCheck %s --check-prefix=RESIDUE
# RUN: llvm-readobj --program-headers %t/residue | FileCheck %s --check-prefix=TLS
# RUN: ld.lld -m elf64mmix_linux --defsym=TLS_START=0x20018 -T %t/residue.ld %t/input.o -o %t/larger-residue
# RUN: llvm-objdump -d %t/larger-residue | FileCheck %s --check-prefix=LARGER-RESIDUE
# RUN: ld.lld -m elf64mmix_linux %t/input.o -o %t/default
# RUN: llvm-readobj --relocations %t/default | FileCheck %s --check-prefix=NORELOC
# RUN: ld.lld -m elf64mmix_linux -r %t/input.o -o %t/partial.o
# RUN: llvm-readobj --relocations %t/partial.o | FileCheck %s --check-prefix=PARTIAL
# RUN: ld.lld -m elf64mmix_linux -T %t/residue.ld %t/partial.o -o %t/roundtrip
# RUN: llvm-objdump -d %t/roundtrip | FileCheck %s --check-prefix=RESIDUE
# RUN: ld.lld -m elf64mmix_linux -T %t/overaligned.ld %t/input.o -o %t/overaligned
# RUN: llvm-objdump -d %t/overaligned | FileCheck %s --check-prefix=OVER
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/zero.s -o %t/zero.o
# RUN: ld.lld -m elf64mmix_linux %t/zero.o -o %t/zero
# RUN: llvm-readobj --program-headers %t/zero | FileCheck %s --check-prefix=ZERO
# RUN: llvm-objdump -d %t/zero | FileCheck %s --check-prefix=ZERO-CODE
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/empty.s -o %t/empty.o
# RUN: ld.lld -m elf64mmix_linux %t/empty.o -o %t/empty
# RUN: llvm-readobj --program-headers %t/empty | FileCheck %s --check-prefix=EMPTY
# RUN: llvm-objdump -d %t/empty | FileCheck %s --check-prefix=EMPTY-CODE

## A=64, R=8, D=72, symbol offsets 24 and 56. The independent instruction
## fragments deliberately do not form adjacent same-register sequences.
# RESIDUE: e3 01 00 68
# RESIDUE: fd 00 00 00
# RESIDUE: e4 04 ff ff
# RESIDUE: e6 02 ff ff
# RESIDUE: e5 03 ff ff
# RESIDUE: e3 05 00 80
# LARGER-RESIDUE: e3 01 00 38
# LARGER-RESIDUE: e3 05 00 40
# TLS: Type: PT_LOAD
# TLS: VirtualAddress: 0x10000
# TLS: Type: PT_LOAD
# TLS-NEXT: Offset: [[TEMPLATE:0x[0-9A-F]+]]
# TLS-NEXT: VirtualAddress: 0x20008
# TLS: FileSize: 32
# TLS: PF_R
# TLS: Type: PT_TLS
# TLS-NEXT: Offset: [[TEMPLATE]]
# TLS: VirtualAddress: 0x20008
# TLS: FileSize: 32
# TLS: MemSize: 64
# TLS: Alignment: 64
# NORELOC: Relocations [
# NORELOC-NEXT: ]
# PARTIAL: R_MMIX_TPREL_LO16 data 0x8
# PARTIAL: R_MMIX_TPREL_HI16 data 0xFFFFFFFFFFFFFF9F
# PARTIAL: R_MMIX_TPREL_ML16 data 0xFFFFFFFFFFFFFF9F
# PARTIAL: R_MMIX_TPREL_MH16 data 0xFFFFFFFFFFFFFF9F
# PARTIAL: R_MMIX_TPREL_LO16 zero 0x0
## A=16384, R=0, D=16384. data is at image offset 24.
# OVER: e3 01 40 20
# ZERO: Type: PT_TLS
# ZERO: FileSize: 0
# ZERO: MemSize: 32
# ZERO: Alignment: 64
# ZERO-CODE: e3 01 00 40
# EMPTY: Type: PT_TLS
# EMPTY: FileSize: 0
# EMPTY: MemSize: 0
# EMPTY-CODE: e3 01 00 10

#--- input.s
.text
.globl _start
_start:
SETL r1, %tprel_lo(data + 8)
SWYM 0,0,0
INCH r4, %tprel_hi(data - 97)
INCML r2, %tprel_ml(data - 97)
INCMH r3, %tprel_mh(data - 97)
SETL r5, %tprel_lo(zero)
.section .tdata,"awT",@progbits
.space 24
.globl data
data: .8byte 42
.section .tbss,"awT",@nobits
.p2align 6
.globl zero
zero: .space 8

#--- residue.ld
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); tls PT_TLS FLAGS(4); }
SECTIONS {
 .text 0x10000 : { *(.text) } :text
 .tdata (DEFINED(TLS_START) ? TLS_START : 0x20008) : { *(.tdata) } :data :tls
 .tbss : { *(.tbss) } :data :tls
}

#--- overaligned.ld
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); tls PT_TLS FLAGS(4); }
SECTIONS {
 .text 0x10000 : { *(.text) } :text
 .tdata 0x20000 : ALIGN(16384) { *(.tdata) } :data :tls
 .tbss : { *(.tbss) } :data :tls
}

#--- zero.s
.text
.globl _start
_start: SETL r1, %tprel_lo(tls)
.section .tbss,"awT",@nobits
.p2align 6
tls: .space 32

#--- empty.s
.text
.globl _start
_start: SETL r1, %tprel_lo(tls)
.section .tdata,"awT",@progbits
tls:
