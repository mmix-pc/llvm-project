# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/main.s -o %t/main.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: ld.lld -m elf64mmix_linux -shared -soname libprovider.so %t/provider.o -o %t/provider.so
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start %t/main.o %t/provider.so -o %t/exec
# RUN: %python %t/check.py %t/exec 2
# RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start %t/main.o %t/provider.so -o %t/pie
# RUN: %python %t/check.py %t/pie 3
# RUN: ld.lld -m elf64mmix_linux -pie -s --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start %t/main.o %t/provider.so -o %t/stripped
# RUN: %python %t/check.py %t/stripped 3
# RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start %t/main.o %t/provider.o -o %t/self-contained
# RUN: llvm-readobj --file-headers --dynamic-table %t/self-contained | FileCheck %s --check-prefix=SELF
# RUN: not ld.lld -m elf64mmix_linux -pie %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=INTERP
# RUN: not ld.lld -m elf64mmix_linux --dynamic-linker=/lib/wrong.so %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=INTERP
# RUN: not ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ENTRY
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e missing %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ENTRY
# RUN: not ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e local %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXECUTABLE
# RUN: not ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start %t/main.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDEFINED
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start %t/missing.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=INPUT
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start -T %t/no-phdr.ld %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=PHDR
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e start -T %t/no-interp.ld %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=INTERP-HEADER
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 -e 2 %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=ALIGN
# RUN: not ld.lld -m elf64mmix_linux -shared --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/provider.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DSO-INTERP
# RUN: not ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 -z notext -e start %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=TEXT
# RUN: not ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 --pack-dyn-relocs=relr -e start %t/main.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELA
# SELF: Type: SharedObject
# SELF: FLAGS_1 {{.*}}NOW PIE
# ALIGN: entry must be a nonnegative 4-byte-aligned address
# DSO-INTERP: shared objects do not support PT_INTERP
# TEXT: does not support text relocations
# RELA: require unpacked RELA relocations
# INTERP: dynamic executables require interpreter /lib/ld-llvm-mmix.so.1
# ENTRY: MMIX Linux requires a defined entry symbol or address
# EXECUTABLE: MMIX Linux entry is not in a loaded executable section
# UNDEFINED: undefined symbol: external
# INPUT: cannot open
# PHDR: dynamic executables require exactly one PT_PHDR
# INTERP-HEADER: dynamic executables require exactly one PT_INTERP

#--- main.s
.text
.globl start
start: SWYM 0,0,0
.section .rodata,"a",@progbits
.p2align 3
.mmix_got_pc64 external
.data
.p2align 3
.globl pointers, local
.hidden local
pointers:
.quad local
.quad external + 8
.weak absent
.quad absent
local: .quad 123

#--- provider.s
.data
.p2align 3
.globl external
.type external,@object
external: .quad 1,2
.size external,16

#--- no-phdr.ld
PHDRS {
  headers PT_LOAD FILEHDR PHDRS FLAGS(4);
  interp PT_INTERP FLAGS(4);
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .interp : { *(.interp) } :headers :interp
  .rodata : { *(.rodata) } :headers
  . = ALIGN(8192);
  .text : { *(.text) } :text
  . = ALIGN(8192);
  .data : { *(.data) } :data
  .dynamic : { *(.dynamic) } :data :dynamic
}
#--- no-interp.ld
SECTIONS { /DISCARD/ : { *(.interp) } }

#--- check.py
import pathlib
import struct
import sys

# Use only program headers and dynamic tags, including for stripped images.
data = pathlib.Path(sys.argv[1]).read_bytes()
header = struct.unpack_from(">16sHHIQQQIHHHHHH", data)
_, typ, machine, _, entry, phoff, _, flags, _, phsize, phnum, _, _, _ = header
assert typ == int(sys.argv[2]) and machine == 80 and flags == 0
assert phsize == 56 and phnum > 0
table = data[phoff:phoff + phnum * phsize]
assert len(table) == phnum * phsize
ph = list(struct.iter_unpack(">II6Q", table))
loads = [p for p in ph if p[0] == 1]

def read(va, size):
    for _, perm, off, base, _, filesz, _, _ in loads:
        if perm & 4 and base <= va and va + size <= base + filesz:
            result = data[off + va - base:off + va - base + size]
            assert len(result) == size
            return result
    raise AssertionError((va, size))

def one(kind):
    matches = [p for p in ph if p[0] == kind]
    assert len(matches) == 1
    return matches[0]

headers = one(6)
assert headers[2] == phoff and headers[5] == headers[6] == len(table)
assert read(headers[3], len(table)) == table
interp = one(3)
assert read(interp[3], interp[5]) == b"/lib/ld-llvm-mmix.so.1\0"
assert any(p[1] & 1 and p[3] <= entry < p[3] + p[5] for p in loads)
assert entry % 4 == 0
assert one(0x6474e551)[1] == 6
dyn = one(2)
tags = dict(struct.iter_unpack(">qQ", read(dyn[3], dyn[5])))
assert 0 in tags and tags[30] == 8
assert tags[0x6ffffffb] == (1 | (0x08000000 if typ == 3 else 0))
strings = read(tags[5], tags[10])
def name(offset):
    return strings[offset:strings.index(0, offset)].decode()
assert name(tags[1]) == "libprovider.so"
assert 4 in tags and 0x6ffffef5 in tags
count = struct.unpack(">II", read(tags[4], 8))[1]
symbols = [struct.unpack(">IBBHQQ", read(tags[6] + i * 24, 24)) for i in range(count)]
rels = list(struct.iter_unpack(">QQq", read(tags[7], tags[8])))
assert tags[9] == 24 and tags[11] == 24
assert any(info & 0xffffffff == 42 and name(symbols[info >> 32][0]) == "external"
           for _, info, _ in rels)
assert any(info & 0xffffffff == 5 and name(symbols[info >> 32][0]) == "external" and addend == 8
           for _, info, addend in rels)
relative = [(place, addend) for place, info, addend in rels if info & 0xffffffff == 41]
assert len(relative) == (1 if typ == 3 else 0)
# A PIE local pointer uses B+A, while an ET_EXEC local pointer is already fixed.
if typ == 3:
    place, addend = relative[0]
    assert struct.unpack(">Q", read(addend, 8))[0] == 123
    for bias in (0x20000, 0x400000):
        assert bias + addend == bias + place + 24
else:
    external_place = next(place for place, info, addend in rels
                          if info & 0xffffffff == 5 and addend == 8)
    local_pointer = struct.unpack(">Q", read(external_place - 8, 8))[0]
    assert local_pointer == external_place + 16
    assert struct.unpack(">Q", read(local_pointer, 8))[0] == 123
