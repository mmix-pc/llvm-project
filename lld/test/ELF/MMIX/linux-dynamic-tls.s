# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/refs.s -o %t/refs.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: ld.lld -m elf64mmix_linux -shared -soname libtls.so %t/provider.o -o %t/libtls.so
# RUN: ld.lld -m elf64mmix_linux -shared %t/refs.o %t/libtls.so -o %t/shared.so
# RUN: %python %t/check.py %t/shared.so shared
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/refs.o %t/libtls.so -o %t/exec
# RUN: %python %t/check.py %t/exec main
# RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/refs.o %t/libtls.so -o %t/pie
# RUN: %python %t/check.py %t/pie main
# RUN: ld.lld -m elf64mmix_linux %t/refs.o %t/provider.o -o %t/static
# RUN: %python %t/check.py %t/static static
# RUN: ld.lld -m elf64mmix_linux -r %t/refs.o -o %t/partial.o
# RUN: ld.lld -m elf64mmix_linux -shared %t/partial.o %t/libtls.so -o %t/partial.so
# RUN: %python %t/check.py %t/partial.so shared
# RUN: ld.lld -m elf64mmix_linux -shared --no-apply-dynamic-relocs %t/refs.o %t/libtls.so -o %t/no-apply.so
# RUN: %python %t/check.py %t/no-apply.so shared
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/entry.s -o %t/entry.o
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/entry.o %t/shared.so %t/libtls.so -o %t/consumer
# RUN: llvm-readobj --dynamic-table %t/shared.so | FileCheck %s
# CHECK: FLAGS {{.*}}BIND_NOW STATIC_TLS

#--- entry.s
.text
.globl _start
_start: POP 0,0

#--- provider.s
.section .tdata,"awT",@progbits
.p2align 5
.globl external
.type external,@tls_object
external: .quad 7
.size external,8

#--- refs.s
.text
.globl _start
_start: POP 0,0
.section .tdata,"awT",@progbits
.p2align 5
.zero 16
.globl local, local2, visible
.hidden local, local2
.type local,@tls_object
.type local2,@tls_object
.type visible,@tls_object
local: .quad 11
.size local,8
local2: .quad 12
.size local2,8
visible: .quad 13
.size visible,8
.section .refs,"a",@progbits
.p2align 3
.mmix_gottprel_pc64 local
.mmix_gottprel_pc64 local + 8
.mmix_gottprel_pc64 local - 8
.mmix_gottprel_pc64 visible
.mmix_gottprel_pc64 external
.mmix_tlsgd_pc64 local
.mmix_tlsgd_pc64 local + 8
.mmix_tlsgd_pc64 visible
.mmix_tlsgd_pc64 external
.mmix_tlsld_pc64 local
.mmix_tlsld_pc64 local2 + 8
.mmix_dtpoff64 local + 3
.mmix_dtpoff64 local2 - 1

#--- check.py
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
role = sys.argv[2]
header = struct.unpack_from('>16sHHIQQQIHHHHHH', data)
ph = [struct.unpack_from('>II6Q', data, header[5] + i * 56)
      for i in range(header[10])]
sh = [struct.unpack_from('>II4QII2Q', data, header[6] + i * 64)
      for i in range(header[12])]
names = sh[header[13]]
strings = data[names[4]:names[4] + names[5]]
def name(n):
    return strings[n:strings.index(b'\0', n)].decode()
sections = {name(s[0]): s for s in sh}
got, refs = sections['.got'], sections['.refs']
def word(va):
    for p in ph:
        if p[0] == 1 and p[3] <= va and va + 8 <= p[3] + p[5]:
            return struct.unpack_from('>Q', data, p[2] + va - p[3])[0]
    raise AssertionError(hex(va))
def slot(i):
    place = refs[3] + i * 8
    delta = word(place)
    if delta >= 1 << 63:
        delta -= 1 << 64
    return place + delta
assert slot(1) == slot(0) + 8 and slot(2) == slot(0) - 8
assert slot(6) == slot(5) + 8
assert slot(10) == slot(9) + 8  # One LD descriptor per output module.
assert word(refs[3] + 11 * 8) == 19
assert word(refs[3] + 12 * 8) == 23
entries = [slot(i) for i in (0, 3, 4, 5, 7, 8, 9)]
assert len(set(entries)) == 7
assert got[5] == (3 + 2 * 4) * 8  # Three IE slots, three GD and one LD pair.
assert all(got[3] <= p < got[3] + got[5] and p % 8 == 0 for p in entries)
rels = {}
dyn = sections.get('.dynsym')
def symname(index):
    sym = struct.unpack_from('>IBBHQQ', data, dyn[4] + index * 24)
    assert sym[1] & 15 == 6  # STT_TLS, never an ordinary pointer provider.
    st = sh[dyn[6]]
    start = st[4] + sym[0]
    return data[start:data.index(b'\0', start)].decode()
rela = sections.get('.rela.dyn')
if rela:
    for off in range(rela[4], rela[4] + rela[5], 24):
        p, info, a = struct.unpack_from('>QQq', data, off)
        assert p not in rels and got[3] <= p <= got[3] + got[5] - 8
        assert any(q[0] == 1 and q[1] == 6 and q[3] <= p and p + 8 <= q[3] + q[6] for q in ph)
        rels[p] = (info & 0xffffffff, symname(info >> 32) if info >> 32 else None, a)
def expect_reloc(p, kind, sym, addend):
    assert rels.pop(p) == (kind, sym, addend)
if role == 'shared':
    expect_reloc(slot(0), 46, None, 16)
    expect_reloc(slot(3), 46, 'visible', 0)
    expect_reloc(slot(5), 44, None, 0)
    expect_reloc(slot(7), 44, 'visible', 0)
    expect_reloc(slot(7) + 8, 45, 'visible', 0)
    expect_reloc(slot(9), 44, None, 0)
else:
    assert word(slot(0)) == 32 + 16
    assert word(slot(3)) == 32 + 32
    assert word(slot(5)) == word(slot(7)) == word(slot(9)) == 1
    assert word(slot(7) + 8) == 32
assert word(slot(5) + 8) == 16 and word(slot(9) + 8) == 0
if role == 'static':
    assert word(slot(4)) == 32 + 64
    assert word(slot(8)) == 1 and word(slot(8) + 8) == 64
else:
    expect_reloc(slot(4), 46, 'external', 0)
    expect_reloc(slot(8), 44, 'external', 0)
    expect_reloc(slot(8) + 8, 45, 'external', 0)
assert not rels
