# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux -T %t/layout.ld %t/input.o -o %t/out
# RUN: llvm-objcopy --dump-section=.refs=%t/refs --dump-section=.got=%t/got %t/out
# RUN: %python %t/check.py %t/refs %t/got
# RUN: ld.lld -m elf64mmix_linux --defsym=got_base=0x50000 -T %t/layout.ld %t/input.o -o %t/positive
# RUN: llvm-objcopy --dump-section=.refs=%t/refs --dump-section=.got=%t/got %t/positive
# RUN: %python %t/check.py %t/refs %t/got 0x50000
# RUN: ld.lld -m elf64mmix_linux -r %t/input.o -o %t/partial.o
# RUN: ld.lld -m elf64mmix_linux -T %t/layout.ld %t/partial.o -o %t/again
# RUN: llvm-objcopy --dump-section=.refs=%t/refs --dump-section=.got=%t/got %t/again
# RUN: %python %t/check.py %t/refs %t/got
# RUN: not ld.lld -m elf64mmix -T %t/layout.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=GENERIC
# GENERIC: R_MMIX_GOT_PC64 requires MMIX Linux output

## Reconstruct addresses from the literal and P, then inspect the selected GOT
## word. Do not assume the order in which the generic linker allocates slots.
## Repeated references and distinct addends share a slot; undefined weak is zero.
#--- check.py
import pathlib
import struct
import sys

refs = struct.unpack(">7q", pathlib.Path(sys.argv[1]).read_bytes())
got = struct.unpack(">3Q", pathlib.Path(sys.argv[2]).read_bytes())
got_base = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x20000
slots = [0x30000 + i * 8 + refs[i] - a
         for i, a in enumerate([0, 8, -8, 0, 0])]
assert slots[0] == slots[1] == slots[2]
assert len(set(slots)) == 3
for slot, expected in zip(slots, [0x123456000] * 3 + [0x10000, 0]):
    assert slot % 8 == 0 and got_base <= slot < got_base + 24
    assert got[(slot - got_base) // 8] == expected
assert 0x30028 + refs[5] == 0x123456007
assert 0x30030 + refs[6] == 0xffff

#--- layout.ld
ENTRY(_start)
PROVIDE(got_base = 0x20000);
SECTIONS {
  .text 0x10000 : { *(.text) }
  .got got_base : { *(.got) }
  .refs 0x30000 : { *(.refs) }
  .far 0x123456000 : { *(.far) }
}
#--- input.s
.text
.globl _start
.type _start,@function
_start:
SWYM 0,0,0
.section .refs,"a",@progbits
.p2align 3
.mmix_got_pc64 object
.mmix_got_pc64 object + 8
.mmix_got_pc64 object - 8
.mmix_got_pc64 _start
.weak absent
.mmix_got_pc64 absent
.quad object + 7 - .
.quad _start - 1 - .
.section .far,"a",@progbits
.globl object
.type object,@object
object:
.quad 42
