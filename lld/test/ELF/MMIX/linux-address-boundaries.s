# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/expanding.s -o %t/expanding.o
# RUN: ld.lld -m elf64mmix_linux -T %t/layout.ld %t/expanding.o -o %t/linux
# RUN: llvm-objdump --no-print-imm-hex -d %t/linux | FileCheck %s --check-prefix=EXPAND
# RUN: llvm-readobj --sections --relocations %t/linux | FileCheck %s --check-prefix=CLOSED --implicit-check-not=.MMIX.reg_contents
# RUN: ld.lld -m elf64mmix -T %t/layout.ld %t/expanding.o -o %t/generic
# RUN: llvm-objdump --no-print-imm-hex -d %t/generic | FileCheck %s --check-prefix=EXPAND
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/terminal.s -o %t/terminal.o
# RUN: not ld.lld -m elf64mmix_linux -T %t/layout.ld %t/terminal.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=TERMINAL

## Direct GETA displacement bounds are [-262144, 262140] bytes. One word
## beyond either bound must expand, not truncate. All target addresses here
## are nonnegative; this does not test wrapping through kernel addresses.
# EXPAND: GETAB r1, -65536
# EXPAND: GETA r2, 65535
# EXPAND: SETL r3, 28
# EXPAND-NEXT: {{.*}} INCML r3, 4
# EXPAND-NEXT: {{.*}} INCMH r3, 0
# EXPAND-NEXT: {{.*}} INCH r3, 0
# EXPAND: SETL r4, 48
# EXPAND-NEXT: {{.*}} INCML r4, 12
# EXPAND-NEXT: {{.*}} INCMH r4, 0
# EXPAND-NEXT: {{.*}} INCH r4, 0
# EXPAND: SETL r5, 65535
# EXPAND-NEXT: {{.*}} INCML r5, 65535
# EXPAND-NEXT: {{.*}} INCMH r5, 65535
# EXPAND-NEXT: {{.*}} INCH r5, 32767
# EXPAND: SETL r6, 0
# EXPAND-NEXT: {{.*}} INCML r6, 0
# EXPAND-NEXT: {{.*}} INCMH r6, 0
# EXPAND-NEXT: {{.*}} INCH r6, 0
# CLOSED: Relocations [
# CLOSED-NEXT: ]
# TERMINAL-DAG: relocation R_MMIX_ADDR19 out of range: -262148 is not in [-262144, 262140]
# TERMINAL-DAG: relocation R_MMIX_ADDR19 out of range: 262144 is not in [-262144, 262140]
# TERMINAL-DAG: improper alignment for relocation R_MMIX_ADDR19: 0x2 is not aligned to 4 bytes

#--- layout.ld
ENTRY(_start)
SECTIONS { .text 0x80000 : { *(.text) } }
lower = 0x40000;
upper = 0xc000c;
below = 0x4001c;
above = 0xc0030;
highest = 0x7fffffffffffffff;
terminal_below = 0x3fffc;
terminal_above = 0xc0004;
terminal_unaligned = 0x8000a;

#--- expanding.s
.global _start
_start:
GETA r1, %geta(lower)
GETA r2, %geta(upper)
GETA r3, %geta(below)
GETA r4, %geta(above)
GETA r5, %geta(highest)
GETA r6, %geta(weak_missing)
.weak weak_missing

#--- terminal.s
.global _start
_start:
GETA r1, terminal_below
GETA r2, terminal_above
GETA r3, terminal_unaligned
