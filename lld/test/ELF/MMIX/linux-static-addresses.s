# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/use.s -o %t/use.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/definitions.s -o %t/definitions.o
# RUN: ld.lld -m elf64mmix_linux -z max-page-size=8192 -T %t/layout.ld %t/use.o %t/definitions.o -o %t/linux
# RUN: llvm-objdump -s --section=.text --section=.data %t/linux | FileCheck %s --check-prefix=BYTES
# RUN: llvm-readobj --sections --relocations %t/linux | FileCheck %s --check-prefix=CLOSED --implicit-check-not=.MMIX.reg_contents
# RUN: ld.lld -m elf64mmix -T %t/layout.ld %t/use.o %t/definitions.o -o %t/generic
# RUN: llvm-objdump -s --section=.text --section=.data %t/generic | FileCheck %s --check-prefix=BYTES
# RUN: not ld.lld -m elf64mmix_linux -T %t/layout.ld %t/use.o -o %t/missing 2>&1 | FileCheck %s --check-prefix=MISSING

## Check resolved fields, not just relocation disappearance. Near data and
## function addresses use PC-relative GETA. Far and byte-unaligned addresses
## use four wyde instructions in the destination register, without GREGs.
# BYTES: Contents of section .text:
# BYTES-NEXT: 50000 f4014000 fd000000 fd000000 fd000000
# BYTES-NEXT: 50010 e3020001 e6025566 e5023344 e4021122
# BYTES-NEXT: 50020 e303ffff e6030005 e5030000 e4030000
# BYTES-NEXT: 50030 f4040004 fd000000 fd000000 fd000000
# BYTES-NEXT: 50040 f8000000
# BYTES: Contents of section .data:
# BYTES-NEXT: 60000 00000000 00060001 00000000 00050040
# BYTES-NEXT: 60010 11223344 55660001 00000000 00060018
# BYTES-NEXT: 60020 00000000 0000002a
# CLOSED: Relocations [
# CLOSED-NEXT: ]
# MISSING-DAG: undefined symbol: near_data
# MISSING-DAG: undefined symbol: far_data
# MISSING-DAG: undefined symbol: target

#--- layout.ld
ENTRY(_start)
SECTIONS {
  .text 0x50000 : { *(.text) }
  .data 0x60000 : { *(.data) }
  .far 0x1122334455660000 : { *(.far) }
}

#--- use.s
.text
.global _start
_start:
GETA r1, %geta(near_data - 32)
GETA r2, %geta(far_data + 1)
GETA r3, %geta(near_data - 33)
GETA r4, %geta(target)
.data
.quad near_data - 31
.quad target
.quad far_data + 1
local_pointer:
.quad local_pointer

#--- definitions.s
.text
.global target
target:
POP 0, 0
.data
.global near_data
near_data:
.quad 42
.section .far,"a",@progbits
.global far_data
far_data:
.quad 0
