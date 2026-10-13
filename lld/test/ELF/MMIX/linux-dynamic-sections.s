# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/main.s -o %t/main.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/member.s -o %t/member.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/loser.s -o %t/loser.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/unused.s -o %t/unused.o
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: llvm-ar rcs %t/members.a %t/member.o %t/unused.o
# RUN: ld.lld -m elf64mmix_linux -shared -soname libprovider.so %t/provider.o -o %t/provider.so
# RUN: ld.lld -m elf64mmix_linux -shared -soname libunused.so %t/unused.o -o %t/unused.so
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --gc-sections -T %t/aliases.ld %t/main.o %t/members.a %t/loser.o --as-needed %t/provider.so %t/unused.so -o %t/direct.so
# RUN: %python %t/check.py %t/direct.so
# RUN: ld.lld -m elf64mmix_linux -r %t/main.o %t/members.a %t/loser.o -o %t/partial.o
# RUN: llvm-readobj --relocations --section-groups %t/partial.o | FileCheck %s --check-prefix=PARTIAL
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --gc-sections -T %t/aliases.ld %t/partial.o --as-needed %t/provider.so %t/unused.so -o %t/partial.so
# RUN: %python %t/check.py %t/partial.so
# RUN: ld.lld -m elf64mmix_linux -r %t/partial.o -o %t/again.o
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --gc-sections -T %t/aliases.ld %t/again.o --as-needed %t/provider.so %t/unused.so -o %t/again.so
# RUN: %python %t/check.py %t/again.so
# RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 --export-dynamic -e chosen --gc-sections -T %t/aliases.ld %t/partial.o --as-needed %t/provider.so %t/unused.so -o %t/pie
# RUN: %python %t/check.py %t/pie pie
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 --export-dynamic -e chosen --gc-sections -T %t/aliases.ld %t/partial.o --as-needed %t/provider.so %t/unused.so -o %t/exec
# RUN: %python %t/check.py %t/exec exec
# RUN: llvm-ar rcsT %t/thin.a %t/member.o %t/unused.o
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --gc-sections -T %t/aliases.ld %t/main.o %t/thin.a %t/loser.o --as-needed %t/provider.so %t/unused.so -o %t/thin.so
# RUN: %python %t/check.py %t/thin.so
# RUN: ld.lld -m elf64mmix_linux -shared -z defs --gc-sections -T %t/aliases.ld %t/main.o %t/members.a %t/loser.o --no-as-needed %t/provider.so %t/unused.so -o %t/all.so
# RUN: llvm-readobj --needed-libs %t/all.so | FileCheck %s --check-prefix=ALL
# RUN: ld.lld -m elf64mmix_linux -shared --whole-archive %t/members.a --no-whole-archive -z defs %t/provider.so -o %t/whole.so
# RUN: llvm-readobj --dyn-symbols %t/whole.so | FileCheck %s --check-prefix=WHOLE
# RUN: not ld.lld -m elf64mmix_linux -shared -z defs --gc-sections -T %t/aliases.ld %t/partial.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDEFINED
# PARTIAL: R_MMIX_GOT_PC64 chosen 0x0
# PARTIAL: R_MMIX_GOT_PC64 visible 0x0
# PARTIAL: R_MMIX_GOT_PC64 visible 0x8
# PARTIAL: R_MMIX_GOT_PC64 script_alias 0x0
# PARTIAL: R_MMIX_GOT_PC64 hidden_alias 0x0
# PARTIAL: R_MMIX_GOT_PC64 external 0x0
# PARTIAL: R_MMIX_64 chosen 0x0
# PARTIAL: R_MMIX_64 visible 0x0
# PARTIAL: Signature: chosen
# ALL-DAG: libprovider.so
# ALL-DAG: libunused.so
# WHOLE: Name: unused_member
# UNDEFINED: undefined symbol: external

#--- main.s
.section .refs,"a",@progbits
.p2align 3
.globl refs
refs:
.mmix_got_pc64 chosen
.mmix_got_pc64 visible
.mmix_got_pc64 visible + 8
.mmix_got_pc64 script_alias
.mmix_got_pc64 hidden_alias
.mmix_got_pc64 external
.section .data.rel.ro,"aw",@progbits
.p2align 3
.globl pointers
pointers:
.quad chosen
.quad visible
.section .data.visible,"aw",@progbits
.p2align 3
.globl visible
.type visible,@object
visible: .quad 1,2,3
.section .dead,"a",@progbits
.p2align 3
.mmix_got_pc64 dead_reference
.section .kept,"a",@progbits
.quad 123

#--- member.s
.section .text.chosen,"axG",@progbits,chosen,comdat
.p2align 3
.globl chosen
.type chosen,@function
chosen:
SWYM 0,0,0
SWYM 0,0,0
.mmix_got_pc64 external

#--- loser.s
.section .text.chosen,"axG",@progbits,chosen,comdat
.p2align 3
.globl chosen
.type chosen,@function
chosen:
.mmix_got_pc64 discarded_reference

#--- unused.s
.data
.globl unused_member
unused_member: .quad 99

#--- provider.s
.data
.p2align 3
.globl external
.type external,@object
external: .quad 99

#--- aliases.ld
SECTIONS {
  .kept : { KEEP(*(.kept)) }
  script_alias = visible + 8;
  HIDDEN(hidden_alias = visible + 16);
} INSERT AFTER .refs;

#--- check.py
import pathlib
import struct
import sys
data = pathlib.Path(sys.argv[1]).read_bytes()
mode = sys.argv[2] if len(sys.argv) > 2 else "shared"
h = struct.unpack_from(">16sHHIQQQIHHHHHH", data)
ph = [struct.unpack_from(">II6Q", data, h[5] + i * h[9]) for i in range(h[10])]
loads = [p for p in ph if p[0] == 1]
def read(va, size):
    for p in loads:
        if p[3] <= va and va + size <= p[3] + p[5]:
            off = p[2] + va - p[3]
            return data[off:off + size]
    raise AssertionError((va, size))
d = next(p for p in ph if p[0] == 2)
entries = list(struct.iter_unpack(">qQ", read(d[3], d[5])))
tags = dict(entries)
strings = read(tags[5], tags[10])
def name(off):
    return strings[off:strings.index(0, off)].decode()
assert [name(value) for tag, value in entries if tag == 1] == ["libprovider.so"]
count = struct.unpack(">II", read(tags[4], 8))[1]
syms = [struct.unpack(">IBBHQQ", read(tags[6] + i * 24, 24)) for i in range(count)]
symbols = {name(s[0]): s for s in syms}
assert not {"unused_member", "dead_reference", "discarded_reference", "hidden_alias"} & symbols.keys()
for key in ("refs", "chosen", "visible", "script_alias", "pointers"):
    assert symbols[key][3] != 0
assert symbols["external"][3] == 0
assert symbols["script_alias"][4] == symbols["visible"][4] + 8
records = {}
for place, info, addend in struct.iter_unpack(">QQq", read(tags[7], tags[8])):
    records[place] = (info & 0xffffffff, name(syms[info >> 32][0]), addend)
refs = symbols["refs"][4]
slots = []
for i, addend in enumerate((0, 0, 8, 0, 0, 0)):
    place = refs + i * 8
    slots.append(place + struct.unpack(">q", read(place, 8))[0] - addend)
assert slots[1] == slots[2]
expected = [
    (42, "chosen", 0), (42, "visible", 0), (42, "visible", 0),
    (42, "script_alias", 0), (41, "", symbols["visible"][4] + 16),
    (42, "external", 0)]
if mode == "shared":
    assert [records[s] for s in slots] == expected
else:
    assert records[slots[5]] == (42, "external", 0)
    values = [symbols[key][4] for key in ("chosen", "visible", "visible", "script_alias")]
    values.append(symbols["visible"][4] + 16)
    for slot, value in zip(slots, values):
        if mode == "pie":
            assert records[slot] == (41, "", value)
        else:
            assert slot not in records
            assert struct.unpack(">Q", read(slot, 8))[0] == value
chosen = symbols["chosen"][4]
assert read(chosen, 8) == bytes.fromhex("fd000000fd000000")
assert chosen + 8 + struct.unpack(">q", read(chosen + 8, 8))[0] == slots[5]
pointers = symbols["pointers"][4]
for place, key in ((pointers, "chosen"), (pointers + 8, "visible")):
    if mode == "shared":
        assert records[place] == (5, key, 0)
    elif mode == "pie":
        assert records[place] == (41, "", symbols[key][4])
    else:
        assert place not in records
        assert struct.unpack(">Q", read(place, 8))[0] == symbols[key][4]
sh = [struct.unpack_from(">II4QII2Q", data, h[6] + i * h[11]) for i in range(h[12])]
names = data[sh[h[13]][4]:sh[h[13]][4] + sh[h[13]][5]]
sections = {names[s[0]:names.index(0, s[0])].decode(): s for s in sh}
assert ".dead" not in sections
kept = sections[".kept"]
assert read(kept[3], 8) == struct.pack(">Q", 123)
