# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/valid.s -o %t/valid.o
# RUN: ld.lld -m elf64mmix_linux -shared %t/valid.o -o %t/valid.so
# RUN: %python %t/check.py %t/valid.so
# RUN: ld.lld -m elf64mmix_linux -r %t/valid.o -o %t/partial.o
# RUN: ld.lld -m elf64mmix_linux -shared %t/partial.o -o %t/partial.so
# RUN: %python %t/check.py %t/partial.so
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/invalid.s -o %t/invalid.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/invalid.o -o /dev/null 2>&1 | FileCheck %s
# RUN: yaml2obj %t/truncated.yaml -o %t/truncated.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/truncated.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=TRUNC
# CHECK-DAG: (.rodata): relocation R_MMIX_PC_64 at offset 1 requires a complete aligned 8-byte literal
# CHECK-DAG: (.gcc_except_table.executable): relocation R_MMIX_PC_64 at offset 1 requires a complete aligned 8-byte literal
# TRUNC: (.gcc_except_table): relocation R_MMIX_PC_64 at offset 1 requires a complete aligned 8-byte literal

#--- valid.s
.text
.hidden target
.globl target
target:
POP 0,0
.section .gcc_except_table,"a",@progbits
.byte 0
.quad target - .
.section .gcc_except_table.function,"a",@progbits
.byte 0,0,0,0
.quad target - .

#--- invalid.s
.text
target:
POP 0,0
.section .rodata,"a",@progbits
.byte 0
.quad target - .
.section .gcc_except_table.executable,"ax",@progbits
.byte 0
.quad target - .

#--- truncated.yaml
--- !ELF
FileHeader:
  Class: ELFCLASS64
  Data: ELFDATA2MSB
  Type: ET_REL
  Machine: EM_MMIX
Sections:
  - Name: .gcc_except_table
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 1
    Content: '0000000000'
  - Name: .rela.gcc_except_table
    Type: SHT_RELA
    Link: .symtab
    Info: .gcc_except_table
    Relocations:
      - Offset: 1
        Type: R_MMIX_PC_64
        Symbol: target
Symbols:
  - Name: target
    Binding: STB_LOCAL
    Index: SHN_ABS
    Value: 0

#--- check.py
import struct, sys
data = open(sys.argv[1], "rb").read()
shoff = struct.unpack_from(">Q", data, 40)[0]
count, names = struct.unpack_from(">HH", data, 60)
sections = [struct.unpack_from(">IIQQQQIIQQ", data, shoff + i * 64) for i in range(count)]
strings = sections[names]
def name(section):
    start = strings[4] + section[0]
    return data[start:data.index(b"\0", start)].decode()
text = next(s for s in sections if name(s) == ".text")
lsda = next(s for s in sections if name(s) == ".gcc_except_table")
# The linker concatenates both byte-packed input sections without padding.
assert lsda[5] == 21
for offset in (1, 13):
    relative = struct.unpack_from(">q", data, lsda[4] + offset)[0]
    assert lsda[3] + offset + relative == text[3]
