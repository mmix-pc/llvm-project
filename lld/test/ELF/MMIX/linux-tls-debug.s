# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix -filetype=obj %t/good.s -o %t.o
# RUN: ld.lld -m elf64mmix_linux -shared %t.o -o %t.so
# RUN: llvm-readobj --hex-dump=.debug_info %t.so | FileCheck %s
# RUN: ld.lld -m elf64mmix_linux -static %t.o -o %t.exe
# RUN: llvm-readobj --hex-dump=.debug_info %t.exe | FileCheck %s
# RUN: ld.lld -m elf64mmix_linux -r %t.o -o %t.rel
# RUN: ld.lld -m elf64mmix_linux -shared %t.rel -o %t.rel.so
# RUN: llvm-readobj --hex-dump=.debug_info %t.rel.so | FileCheck %s
# RUN: ld.lld -m elf64mmix_linux -static --gc-sections %t.o -o %t.gc
# RUN: llvm-readobj --hex-dump=.debug_info %t.gc | FileCheck %s --check-prefix=GC
# RUN: llvm-mc -triple=mmix -filetype=obj %t/bad.s -o %t.bad.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t.o %t.bad.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
# CHECK: 0x00000000 aa000000 00000000 0b000000 00000000
# CHECK: 0x00000010 10
# GC: 0x00000000 aa000000 00000000 00000000 00000000
# GC: 0x00000010 00
# BAD: R_MMIX_64 against TLS symbol tls requires a non-allocated debug section

#--- good.s
.text
.globl _start
_start: SWYM 0,0,0
.section .tdata,"awT",@progbits
.p2align 3
.quad 99
.globl tls
.type tls,@tls_object
tls: .quad 42
.section .tbss,"awT",@nobits
.p2align 3
.globl zero
.type zero,@tls_object
zero: .zero 8
.section .debug_info,"",@progbits
.byte 0xaa
.quad tls+3
.quad zero

#--- bad.s
.data
.quad tls
.type tls,@tls_object
