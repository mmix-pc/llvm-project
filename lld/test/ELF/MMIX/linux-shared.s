# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/definitions.s -o %t/definitions.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/references.s -o %t/references.o
# RUN: ld.lld -m elf64mmix_linux -shared --hash-style=gnu -soname libsample.so %t/definitions.o %t/references.o -o %t/sample.so
# RUN: llvm-readobj --file-headers --dynamic-table --relocations %t/sample.so | FileCheck %s
# RUN: llvm-objdump -s -j .refs %t/sample.so | FileCheck %s --check-prefix=DUMP
# RUN: %python %t/check.py %t/sample.so
# RUN: ld.lld -m elf64mmix_linux -shared --hash-style=sysv -soname libsample.so -s %t/definitions.o %t/references.o -o %t/stripped.so
# RUN: %python %t/check.py %t/stripped.so
# RUN: ld.lld -m elf64mmix_linux -shared --no-apply-dynamic-relocs -soname libsample.so %t/definitions.o %t/references.o -o %t/no-addends.so
# RUN: %python %t/check.py %t/no-addends.so
# CHECK: Type: SharedObject
# CHECK: Entry: 0x0
# CHECK-DAG: SONAME {{.*}}libsample.so
# CHECK-DAG: FLAGS {{.*}}BIND_NOW
# CHECK-DAG: FLAGS_1 {{.*}}NOW
# CHECK-DAG: RELACOUNT {{.*}}4
# CHECK-DAG: R_MMIX_RELATIVE
# CHECK-DAG: R_MMIX_GLOB_DAT
# CHECK-DAG: R_MMIX_64
# DUMP: Contents of section .refs:

#--- definitions.s
.text
.globl function, _start
.type function,@function
function:
_start:
POP 0,0
.data
.p2align 3
.globl visible, hidden, protected
.type visible,@object
.type hidden,@object
.type protected,@object
.hidden hidden
.protected protected
visible: .quad 0x123456789abcdef0
hidden: .quad 2
protected: .quad 3

#--- references.s
.section .refs,"a",@progbits
.p2align 3
.globl refs
refs:
.mmix_got_pc64 visible
.mmix_got_pc64 visible + 8
.mmix_got_pc64 visible - 8
.mmix_got_pc64 function
.mmix_got_pc64 hidden
.mmix_got_pc64 protected
.weak absent
.mmix_got_pc64 absent
.quad hidden - .
.section .data.rel.ro,"aw",@progbits
.p2align 3
.globl pointers
pointers:
.quad hidden + 7
.quad protected - 1
.quad visible + 9
.quad function
.quad absent

#--- check.py
import pathlib
import struct
import sys

# Deliberately use only ELF/program headers and dynamic tags. Section headers
# (and the regular symbol table) are not a loader's source of truth.
data = pathlib.Path(sys.argv[1]).read_bytes()
assert data[:7] == b"\x7fELF\x02\x02\x01"
(_, typ, machine, version, entry, phoff, _, flags, ehsize, phsize,
 phnum, _, _, _) = struct.unpack_from(">16sHHIQQQIHHHHHH", data)
assert (typ, machine, version, entry, flags, ehsize, phsize) == (3, 80, 1, 0, 0, 64, 56)
assert 0 < phnum < 0xffff and phoff + phnum * 56 <= len(data)
ph = [struct.unpack_from(">II6Q", data, phoff + i * 56) for i in range(phnum)]
loads = [p for p in ph if p[0] == 1]
for _, perm, off, va, _, filesz, memsz, align in loads:
    assert filesz <= memsz and off + filesz <= len(data)
    assert va + memsz <= 1 << 63 and align >= 8192
    assert va % align == off % align and perm & 4 and perm & 3 != 3
assert not any(p[0] == 3 for p in ph)
assert [(p[1]) for p in ph if p[0] == 0x6474e551] == [6]
assert any(p[1] & 4 and p[2] == 0 and p[5] >= phoff + phnum * 56 for p in loads)

def read(va, size):
    for _, perm, off, base, _, filesz, _, _ in loads:
        if perm & 4 and base <= va and va + size <= base + filesz:
            return data[off + va - base:off + va - base + size]
    raise AssertionError((hex(va), size))

dynamic = [p for p in ph if p[0] == 2]
assert len(dynamic) == 1
_, _, off, va, _, size, memsize, _ = dynamic[0]
assert size % 16 == 0 and size <= memsize
assert read(va, size) == data[off:off + size]
tags = {}
for pos in range(0, size, 16):
    tag, val = struct.unpack_from(">qQ", data, off + pos)
    assert tag not in tags
    tags[tag] = val
    if tag == 0:
        break
assert 0 in tags
assert set(tags) <= {0, 4, 5, 6, 7, 8, 9, 10, 11, 14, 30,
                     0x6ffffef5, 0x6ffffff9, 0x6ffffffb}
assert tags[30] == 8 and tags[0x6ffffffb] == 1
assert tags[9] == 24 and tags[11] == 24
strings = read(tags[5], tags[10])
def name(i):
    end = strings.index(0, i)
    return strings[i:end].decode()
assert name(tags[14]) == "libsample.so"
nbucket, count = struct.unpack(">II", read(tags[4], 8))
hashes = struct.unpack(f">{nbucket + count}I", read(tags[4] + 8, (nbucket + count) * 4))
assert all(i < count for i in hashes)
symbols = [struct.unpack(">IBBHQQ", read(tags[6] + i * 24, 24)) for i in range(count)]
byname = {name(s[0]): s for s in symbols}
assert "hidden" not in byname
assert byname["protected"][2] == 3
assert byname["absent"][1] >> 4 == 2 and byname["absent"][3] == 0
# Validate GNU hash bucket and chain bounds without the section table.
nb, first, bloom, shift = struct.unpack(">4I", read(tags[0x6ffffef5], 16))
assert nb > 0 and bloom > 0 and bloom & (bloom - 1) == 0 and shift < 64
buckets = struct.unpack(f">{nb}I", read(tags[0x6ffffef5] + 16 + bloom * 8, nb * 4))
chains = tags[0x6ffffef5] + 16 + bloom * 8 + nb * 4
for b in buckets:
    if b:
        assert first <= b < count
        while True:
            assert b < count
            if struct.unpack(">I", read(chains + (b - first) * 4, 4))[0] & 1:
                break
            b += 1
rels = [struct.unpack(">QQq", read(tags[7] + i, 24)) for i in range(0, tags[8], 24)]
assert len(rels) == 10 and tags[0x6ffffff9] == 4
records = {}
for i, (place, info, addend) in enumerate(rels):
    sym, typ = info >> 32, info & 0xffffffff
    assert typ in {5, 41, 42} and sym < count and place % 8 == 0
    assert (typ == 41) == (i < 4)
    assert (sym == 0) == (typ == 41)
    assert any(p[1] & 3 == 2 and p[3] <= place and place + 8 <= p[3] + p[6] for p in loads)
    assert place not in records
    records[place] = (typ, name(symbols[sym][0]), addend)
for a, b in zip(sorted(records), sorted(records)[1:]):
    assert b - a >= 8
refs = byname["refs"][4]
ptrs = byname["pointers"][4]
hidden = byname["visible"][4] + 8
protected = byname["protected"][4]
slots = []
for i, addend in enumerate([0, 8, -8, 0, 0, 0, 0]):
    place = refs + i * 8
    slot = place + struct.unpack(">q", read(place, 8))[0] - addend
    assert slot in records
    slots.append(slot)
assert slots[0] == slots[1] == slots[2] and len(set(slots)) == 5
expected = [(42, "visible", 0)] * 3 + [(42, "function", 0),
            (41, "", hidden), (41, "", protected), (42, "absent", 0)]
assert [records[s] for s in slots] == expected
assert refs + 56 + struct.unpack(">q", read(refs + 56, 8))[0] == hidden
assert [records[ptrs + i * 8] for i in range(5)] == [
    (41, "", hidden + 7), (41, "", protected - 1), (5, "visible", 9),
    (5, "function", 0), (5, "absent", 0)]
