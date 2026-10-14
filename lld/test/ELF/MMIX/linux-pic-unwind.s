# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -position-independent -filetype=obj %t/frame.s -o %t/frame.o
# RUN: ld.lld -m elf64mmix_linux -shared --eh-frame-hdr %t/frame.o -o %t/frame.so
# RUN: llvm-readobj --relocations --program-headers %t/frame.so | FileCheck %s
# RUN: llvm-dwarfdump --eh-frame %t/frame.so | FileCheck %s --check-prefix=CFI
# RUN: %python %t/check.py %t/frame.so
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/frame.s -o %t/absolute.o
# RUN: not ld.lld -m elf64mmix_linux -shared --eh-frame-hdr %t/absolute.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD
# CHECK: PT_GNU_EH_FRAME
# CHECK: .rela.dyn {
# CHECK: R_MMIX_64 personality
# CHECK-NEXT: }
# CFI: Augmentation: "zPLR"
# CFI: FDE cie=
# CFI: LSDA Address:
# BAD: relocation R_MMIX_64 cannot be used against

#--- frame.s
.text
.globl function, personality
.type function,@function
function:
.cfi_startproc
.cfi_personality 156, personality_slot
.cfi_lsda 28, lsda
POP 0,0
.cfi_endproc
.type personality,@function
personality:
POP 0,0
.section .gcc_except_table,"a",@progbits
lsda:
.byte 255,255,1,0
.section .data.rel.ro,"aw",@progbits
.p2align 3
.hidden personality_slot
personality_slot:
.quad personality

#--- check.py
import struct, sys
b = open(sys.argv[1], "rb").read()
phoff = struct.unpack_from(">Q", b, 32)[0]
phnum = struct.unpack_from(">H", b, 56)[0]
ph = [struct.unpack_from(">IIQQQQQQ", b, phoff + 56*i) for i in range(phnum)]
loads = [p for p in ph if p[0] == 1]
eh = [p for p in ph if p[0] == 0x6474e550]
assert len(eh) == 1 and eh[0][1] == 4
dynamic = next(p for p in ph if p[0] == 2)
tags = dict(struct.unpack_from(">QQ", b, i) for i in range(dynamic[2], dynamic[2]+dynamic[5], 16))
rela = next(p[2]+tags[7]-p[3] for p in loads if p[3] <= tags[7] < p[3]+p[5])
assert tags[8] == 24
where, info, addend = struct.unpack_from(">QQq", b, rela)
assert info & 0xffffffff == 5
assert any(p[1] & 2 and p[3] <= where and where+8 <= p[3]+p[6] for p in loads)
assert not any(p[1] & 1 and p[3] <= where < p[3]+p[6] for p in loads)
