# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --icf=all %t/input.o -o %t/all.so
# RUN: llvm-readobj --elf-output-style=JSON --dyn-symbols %t/all.so > %t/all.json
# RUN: %python %t/check.py %t/all.json folded
# RUN: llvm-readobj --relocations %t/all.so | FileCheck %s
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --icf=safe %t/input.o -o %t/safe.so
# RUN: llvm-readobj --elf-output-style=JSON --dyn-symbols %t/safe.so > %t/safe.json
# RUN: %python %t/check.py %t/safe.json distinct
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --icf=all --keep-unique=one %t/input.o -o %t/unique.so
# RUN: llvm-readobj --elf-output-style=JSON --dyn-symbols %t/unique.so > %t/unique.json
# RUN: %python %t/check.py %t/unique.json distinct
# RUN: ld.lld -m elf64mmix_linux -r %t/input.o -o %t/partial.o
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --icf=all %t/partial.o -o %t/partial.so
# RUN: llvm-readobj --elf-output-style=JSON --dyn-symbols %t/partial.so > %t/partial.json
# RUN: %python %t/check.py %t/partial.json folded
# RUN: llvm-readobj --relocations %t/partial.so | FileCheck %s
# CHECK-DAG: R_MMIX_GLOB_DAT one 0x0
# CHECK-DAG: R_MMIX_GLOB_DAT two 0x0
# CHECK-DAG: R_MMIX_GLOB_DAT three 0x0
# CHECK-DAG: R_MMIX_GLOB_DAT first 0x0
# CHECK-DAG: R_MMIX_GLOB_DAT second 0x0

#--- input.s
.section .text.one,"ax",@progbits
.p2align 3
.globl one
.type one,@function
one:
SWYM 0,0,0
SWYM 0,0,0
.mmix_got_pc64 first
.section .text.two,"ax",@progbits
.p2align 3
.globl two
.type two,@function
two:
SWYM 0,0,0
SWYM 0,0,0
.mmix_got_pc64 second
.section .text.three,"ax",@progbits
.p2align 3
.globl three
.type three,@function
three:
SWYM 0,0,0
SWYM 0,0,0
.mmix_got_pc64 first
.section .data.first,"aw",@progbits
.globl first
.type first,@object
first: .quad 123
.section .data.second,"aw",@progbits
.globl second
.type second,@object
second: .quad 123
.section .refs,"a",@progbits
.p2align 3
.globl refs
refs:
.mmix_got_pc64 one
.mmix_got_pc64 two
.mmix_got_pc64 three

#--- check.py
import json
import sys
rows = json.load(open(sys.argv[1]))[0]["DynamicSymbols"]
symbols = {r["Symbol"]["Name"]["Name"]: r["Symbol"] for r in rows}
def address(name):
    s = symbols[name]
    assert s["Section"]["Value"] != 0
    assert s["Binding"]["Name"] == "Global" and s["Other"]["Value"] == 0
    return s["Value"]
# Different preemptible targets must not compare equal, even with equal data.
assert address("one") != address("two")
assert address("two") != address("three")
# Aggressive ICF may merge code, but must preserve each dynamic symbol/GOT slot.
assert (address("one") == address("three")) == (sys.argv[2] == "folded")
