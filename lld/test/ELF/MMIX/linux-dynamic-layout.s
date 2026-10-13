# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux -shared %t/input.o -o %t/dso
# RUN: %python %t/check.py %t/dso
# RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/input.o -o %t/pie
# RUN: %python %t/check.py %t/pie
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t/input.o %t/dso -o %t/exec
# RUN: %python %t/check.py %t/exec
# RUN: ld.lld -m elf64mmix_linux -shared -z max-page-size=32768 %t/input.o -o %t/large-align
# RUN: %python %t/check.py %t/large-align
# RUN: ld.lld -m elf64mmix_linux -shared -T %t/good.ld %t/input.o -o %t/script
# RUN: %python %t/check.py %t/script
# RUN: ld.lld -m elf64mmix_linux -shared -e _start %t/input.o -o %t/entry
# RUN: %python %t/check.py %t/entry interpreter
# RUN: ld.lld -m elf64mmix_linux -shared -e _start -z max-page-size=32768 %t/input.o -o %t/entry-large-align
# RUN: %python %t/check.py %t/entry-large-align
# RUN: not %python %t/check.py %t/entry-large-align interpreter
# RUN: ld.lld -m elf64mmix_linux -shared -e _start %t/input.o %t/dso -o %t/entry-dependency
# RUN: %python %t/check.py %t/entry-dependency
# RUN: not %python %t/check.py %t/entry-dependency interpreter
# RUN: %python %t/scripts.py %t
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/overlap.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=OVERLAP
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/same-permission.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=OVERLAP
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/relro-start.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELRO
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/relro-end.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELRO
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/mutable-relro.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=MUTABLE
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/duplicate-relro.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUPLICATE
# RUN: not ld.lld -m elf64mmix_linux -shared -z execstack %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK
# RUN: not ld.lld -m elf64mmix_linux -shared -z nognustack %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/wx.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=WX
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/extent.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXTENT
# RUN: not ld.lld -m elf64mmix_linux -shared --no-rosegment %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=NONCODE
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/duplicate-stack.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/duplicate-dynamic.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DYNAMIC
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/duplicate-phdr.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PHDR
# RUN: not ld.lld -m elf64mmix_linux -shared -z max-page-size=4096 -z common-page-size=4096 %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=OVERLAP
# OVERLAP: PT_LOAD ranges overlap after page rounding
# RELRO: RELRO must cover whole pages within a RW/NX load
# MUTABLE: RELRO covers unrelated mutable section
# DUPLICATE: requires at most one PT_GNU_RELRO
# STACK: require a non-executable GNU_STACK
# WX: do not support writable executable loads
# EXTENT: outside the nonnegative user address range
# NONCODE: non-code section requires a non-executable load
# DYNAMIC: require exactly one PT_DYNAMIC
# PHDR: require at most one PT_PHDR

#--- input.s
.text
.globl _start
.hidden _start
_start: SWYM 0,0,0
.section .text.more,"ax",@progbits
SWYM 0,0,0
.section .data.rel.ro,"aw",@progbits
.p2align 3
.quad _start
.data
.p2align 3
.quad 42
.bss
.p2align 3
.space 9000

#--- good.ld
PHDRS {
  phdr PT_PHDR PHDRS FLAGS(4);
  headers PT_LOAD FILEHDR PHDRS FLAGS(4);
  code PT_LOAD FLAGS(5);
  code2 PT_LOAD FLAGS(5);
  protected PT_LOAD FLAGS(6);
  mutable PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
  relro PT_GNU_RELRO FLAGS(4);
  stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) } :headers
  .gnu.hash : { *(.gnu.hash) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .rela.dyn : { *(.rela.dyn) }
  . = 0x2000;
  .text : { *(.text) } :code
  . = 0x4000;
  .text.more : { *(.text.more) } :code2
  . = 0x6000;
  .data.rel.ro : { *(.data.rel.ro) } :protected :relro
  .dynamic : { *(.dynamic) } :protected :relro :dynamic
  .got (NOLOAD) : { . = ALIGN(8192); } :protected :relro
  . = 0x8000;
  .data : { *(.data) } :mutable
  .bss : { *(.bss) } :mutable
}

#--- scripts.py
from pathlib import Path
import sys
root = Path(sys.argv[1])
base = (root / "good.ld").read_text()
variants = {
    "overlap": base.replace(". = 0x2000;", ". = 0x1000;"),
    "same-permission": base.replace(". = 0x4000;", ". = 0x2100;"),
    "relro-start": base.replace(". = 0x6000;", ". = 0x6008;"),
    "relro-end": base.replace(". = ALIGN(8192);", ". = ALIGN(8192) - 8;"),
    "duplicate-relro": base.replace("stack PT_GNU_STACK", "extra PT_GNU_RELRO FLAGS(4); stack PT_GNU_STACK")
                           .replace(":protected :relro", ":protected :relro :extra"),
    "wx": base.replace("code PT_LOAD FLAGS(5)", "code PT_LOAD FLAGS(7)"),
    "extent": base.replace(". = 0x8000;", ". = 0x7ffffffffffff000;"),
    "duplicate-stack": base.replace("stack PT_GNU_STACK FLAGS(6);",
                                    "stack PT_GNU_STACK FLAGS(6); extra PT_GNU_STACK FLAGS(6);"),
    "duplicate-dynamic": base.replace("dynamic PT_DYNAMIC FLAGS(6);",
                                      "dynamic PT_DYNAMIC FLAGS(6); extra PT_DYNAMIC FLAGS(6);")
                             .replace(":relro :dynamic", ":relro :dynamic :extra"),
    "duplicate-phdr": base.replace("phdr PT_PHDR PHDRS FLAGS(4);",
                                   "phdr PT_PHDR PHDRS FLAGS(4); extra PT_PHDR PHDRS FLAGS(4);"),
}
# Put unrelated mutable data in its own output section inside RELRO.
variants["mutable-relro"] = base.replace(
    ".dynamic :", ".mutable : { *(.data) } :protected :relro\n  .dynamic :")
for name, script in variants.items():
    (root / (name + ".ld")).write_text(script)

#--- check.py
import pathlib
import struct
import sys
data = pathlib.Path(sys.argv[1]).read_bytes()
h = struct.unpack_from(">16sHHIQQQIHHHHHH", data)
assert h[0][:7] == b"\x7fELF\x02\x02\x01" and h[2] == 80
off, size, count = h[5], h[9], h[10]
assert size == 56 and 0 < count < 0xffff
ph = [struct.unpack_from(">II6Q", data, off + i * size) for i in range(count)]
loads = sorted((p for p in ph if p[0] == 1), key=lambda p: p[3])
end = 0
for _, flags, pos, va, _, filesz, memsz, align in loads:
    assert 0 <= filesz <= memsz and pos + filesz <= len(data)
    assert va + memsz <= 1 << 63
    assert va % 8192 == pos % 8192
    assert align <= 1 or align & (align - 1) == 0 and va % align == pos % align
    assert flags & 4 and flags & 3 != 3
    assert va // 8192 * 8192 >= end
    end = (va + memsz + 8191) // 8192 * 8192
assert any(p[2] == 0 and p[5] >= off + size * count for p in loads)
def mapped(p):
    return any(load[1] & 4 and p[2] >= load[2] and p[3] >= load[3] and
               p[2] - load[2] == p[3] - load[3] and
               p[2] + p[5] <= load[2] + load[5] for load in loads)
headers = [p for p in ph if p[0] == 6]
assert len(headers) <= 1
for p in headers:
    assert p[2] == off and p[5] >= size * count and p[6] >= size * count
    assert mapped(p)
dynamic = [p for p in ph if p[0] == 2]
assert len(dynamic) == 1
d = dynamic[0]
assert d[5] % 16 == 0 and 0 < d[5] <= d[6] and mapped(d)
tags = dict(struct.iter_unpack(">qQ", data[d[2]:d[2] + d[5]]))
assert 0 in tags
assert [p[1] for p in ph if p[0] == 0x6474e551] == [6]
relro = [p for p in ph if p[0] == 0x6474e552]
assert len(relro) == 1
r = relro[0]
assert r[3] % 8192 == 0 and r[6] % 8192 == 0
assert any(p[1] == 6 and p[3] <= r[3] and r[3] + r[6] <= p[3] + p[6] for p in loads)
# The final mutable load has real zero-fill storage beyond file data.
assert loads[-1][6] - loads[-1][5] >= 9000
if len(sys.argv) > 2:
    # The producer/consumer selects this role, not the presence of -e.
    assert h[1] == 3 and h[4] % 4 == 0
    assert loads[0][2] == loads[0][3] == 0
    assert all(p[7] <= 8192 for p in loads)
    assert not any(p[0] == 3 for p in ph)
    assert len(headers) == 1 and headers[0][2] == off
    assert headers[0][5] >= size * count and headers[0][6] >= size * count
    assert any(p[1] & 1 and p[3] <= h[4] < p[3] + p[5] for p in loads)
    assert 0 in tags and 1 not in tags
    def read(va, length):
        for p in loads:
            if p[3] <= va and va + length <= p[3] + p[5]:
                start = p[2] + va - p[3]
                return data[start:start + length]
        raise AssertionError("unmapped runtime table")
    if 7 in tags:
        assert tags[9] == 24
        for _, info, _ in struct.iter_unpack(">QQq", read(tags[7], tags[8])):
            assert info == 41 # Symbol-free R_MMIX_RELATIVE only.
