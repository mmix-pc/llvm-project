# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/main.s -o %t/main.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: ld.lld -m elf64mmix_linux -shared -soname libtls.so %t/provider.o -o %t/libtls.so
# RUN: ld.lld -m elf64mmix_linux --export-dynamic --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/main.o %t/libtls.so -o %t/exec
# RUN: %python %t/check.py %t/exec
# RUN: ld.lld -m elf64mmix_linux -pie --export-dynamic --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/main.o %t/libtls.so -o %t/pie
# RUN: %python %t/check.py %t/pie
# RUN: ld.lld -m elf64mmix_linux -r %t/main.o -o %t/partial.o
# RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/partial.o %t/libtls.so -o %t/partial-pie
# RUN: %python %t/check.py %t/partial-pie
# RUN: not ld.lld -m elf64mmix_linux -shared -Bsymbolic %t/main.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DSO
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/import.s -o %t/import.o
# RUN: not ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/import.o %t/libtls.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROVIDER
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/import.o %t/libtls.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROVIDER
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 --unresolved-symbols=ignore-all %t/import.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROVIDER
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/weak.s -o %t/weak.o
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/weak.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROVIDER
# DSO: requires MMIX Linux executable output
# PROVIDER: requires a defined symbol in the executable TLS image

#--- main.s
.text
.globl _start
_start:
SETL r1, %tprel_lo(data)
INCML r1, %tprel_ml(data)
INCMH r1, %tprel_mh(data)
INCH r1, %tprel_hi(data)
SETL r2, %tprel_lo(zero + 7)
INCML r2, %tprel_ml(zero + 7)
INCMH r2, %tprel_mh(zero + 7)
INCH r2, %tprel_hi(zero + 7)
SETL r3, %tprel_lo(data - 4096)
INCML r3, %tprel_ml(data - 4096)
INCMH r3, %tprel_mh(data - 4096)
INCH r3, %tprel_hi(data - 4096)
POP 0,0
.section .refs,"a",@progbits
.p2align 3
.mmix_gottprel_pc64 data
.mmix_gottprel_pc64 zero
.section .tdata,"awT",@progbits
.p2align 6
.space 16
.globl data
.type data,@tls_object
data: .quad 42
.size data,8
.section .tbss,"awT",@nobits
.p2align 7
.globl zero
.hidden zero
.type zero,@tls_object
zero: .space 8
.size zero,8

#--- provider.s
.section .tdata,"awT",@progbits
.p2align 3
.globl data, foreign
.type data,@tls_object
.type foreign,@tls_object
# A DSO definition of the same name must not interpose on executable TLS.
data: .quad 99
foreign: .quad 1

#--- import.s
.text
.globl _start
_start: SETL r1, %tprel_lo(foreign)

#--- weak.s
.text
.weak missing
.globl _start
_start: SETL r1, %tprel_lo(missing)

#--- check.py
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
h = struct.unpack_from('>16sHHIQQQIHHHHHH', data)
ph = [struct.unpack_from('>II6Q', data, h[5] + i * 56) for i in range(h[10])]
sh = [struct.unpack_from('>II4QII2Q', data, h[6] + i * 64) for i in range(h[12])]
names = sh[h[13]]
strings = data[names[4]:names[4] + names[5]]
sections = {strings[s[0]:strings.index(b'\0', s[0])].decode(): s for s in sh}
tls, = [p for p in ph if p[0] == 7]
assert tls[7] == 128 and tls[5] == 24 and tls[6] == 136
bias = 16 + (tls[3] % tls[7] - 16) % tls[7]
assert any(p[0] == 3 for p in ph)  # PT_INTERP: this is a dynamic main image.
text, refs, got = (sections[n] for n in ('.text', '.refs', '.got'))
def fragment_value(index):
    words = struct.unpack_from('>4I', data, text[4] + index * 16)
    assert [v >> 24 for v in words] == [0xe3, 0xe6, 0xe5, 0xe4]
    return sum((v & 0xffff) << (i * 16) for i, v in enumerate(words))
def ie_value(index):
    place = refs[3] + index * 8
    delta = struct.unpack_from('>q', data, refs[4] + index * 8)[0]
    slot = place + delta
    assert got[3] <= slot <= got[3] + got[5] - 8 and slot % 8 == 0
    return struct.unpack_from('>Q', data, got[4] + slot - got[3])[0]
assert fragment_value(0) == ie_value(0) == bias + 16
assert fragment_value(1) == ie_value(1) + 7 == bias + 128 + 7
assert fragment_value(2) == (bias + 16 - 4096) % (1 << 64)
# LE code and same-main IE slots are final constants, with no runtime patches.
assert all(s[5] == 0 for s in sh if s[1] in (4, 9))
