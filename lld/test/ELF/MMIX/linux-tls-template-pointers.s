# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux -shared %t/input.o -o %t/lib.so
# RUN: llvm-readobj --elf-output-style=JSON --program-headers --relocations %t/lib.so > %t/elf.json
# RUN: %python %t/check.py %t/elf.json
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/bad.s -o %t/bad.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/bad.o -o /dev/null 2>&1 | FileCheck %s
# CHECK: MMIX ordinary dynamic pointer cannot reference TLS
# RUN: yaml2obj %t/unbacked.yaml -o %t/unbacked.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/unbacked.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNBACKED
# UNBACKED: MMIX dynamic relocation requires an eight-byte writable non-executable PT_LOAD target

#--- input.s
.data
.p2align 3
.globl local, visible
.hidden local
local: .quad 7
visible: .quad 9
.section .tdata,"awT",@progbits
.p2align 3
.quad local + 1
.quad visible + 2
.section .tbss,"awT",@nobits
.p2align 3
.space 8

#--- bad.s
.section .tdata,"awT",@progbits
.p2align 3
.globl tls
.type tls,@tls_object
tls: .quad 7
.data
.p2align 3
.quad tls

#--- unbacked.yaml
--- !ELF
FileHeader:
  Class: ELFCLASS64
  Data: ELFDATA2MSB
  Type: ET_REL
  Machine: EM_MMIX
Sections:
  - Name: .tbss
    Type: SHT_NOBITS
    Flags: [ SHF_ALLOC, SHF_WRITE, SHF_TLS ]
    AddressAlign: 8
    Size: 8
  - Name: .rela.tbss
    Type: SHT_RELA
    Info: .tbss
    Link: .symtab
    Relocations:
      - { Offset: 0, Type: R_MMIX_64, Symbol: external }
Symbols:
  - { Name: external, Binding: STB_GLOBAL }

#--- check.py
import json
import sys

record = json.load(open(sys.argv[1]))[0]
tls, = [p["ProgramHeader"] for p in record["ProgramHeaders"]
        if p["ProgramHeader"]["Type"]["Value"] == 7]
assert tls["FileSize"] == 16 and tls["MemSize"] == 24
relocs = [r["Relocation"] for section in record["Relocations"] for r in section["Relocs"]]
assert len(relocs) == 2
assert {r["Type"]["Name"] for r in relocs} == {"R_MMIX_RELATIVE", "R_MMIX_64"}
assert {r["Offset"] for r in relocs} == {tls["VirtualAddress"], tls["VirtualAddress"] + 8}
