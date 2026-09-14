# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix -filetype=obj %t/start.s -o %t/start.o
# RUN: yaml2obj %t/base.yaml -o %t/base.o
# RUN: llvm-ar crs %t/lib.a %t/base.o
## Unextracted members do not participate in the link.
# RUN: ld.lld -m elf64mmix_linux %t/start.o %t/lib.a -o %t/unextracted
# RUN: not ld.lld -m elf64mmix_linux --gc-sections %t/start.o %t/base.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: not ld.lld -m elf64mmix_linux -u anchor %t/start.o %t/lib.a -o %t/bad 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: not ld.lld -m elf64mmix_linux --whole-archive %t/lib.a --no-whole-archive %t/start.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: not ld.lld -m elf64mmix_linux -T %t/discard.ld %t/start.o %t/base.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: not ld.lld -m elf64mmix_linux --no-relax %t/start.o %t/base.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: ld.lld -m elf64mmix --gc-sections %t/start.o %t/base.o -o %t/generic
# RUN: not ld.lld -m elf64mmix_linux -T %t/contents.ld %t/start.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=CONTENTS
# RUN: ld.lld -m elf64mmix -T %t/contents.ld %t/start.o -o %t/generic-contents
# BASE: MMIX Linux does not support R_MMIX_BASE_PLUS_OFFSET
# CONTENTS: MMIX Linux does not support register-content output .MMIX.reg_contents

#--- start.s
.global _start
_start:
SWYM 0, 0, 0

#--- base.yaml
--- !ELF
FileHeader: { Class: ELFCLASS64, Data: ELFDATA2MSB, Type: ET_REL, Machine: EM_MMIX }
Sections:
  - Name: .unused
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content: 23AA0000
  - Name: .rela.unused
    Type: SHT_RELA
    Link: .symtab
    Info: .unused
    Relocations:
      - { Offset: 2, Type: R_MMIX_BASE_PLUS_OFFSET, Symbol: address }
Symbols:
  - { Name: address, Index: SHN_ABS, Value: 256 }
  - { Name: anchor, Section: .unused, Binding: STB_GLOBAL }

#--- discard.ld
SECTIONS {
  .text 0x10000 : { *(.text) }
  /DISCARD/ : { *(.unused) }
}

#--- contents.ld
SECTIONS {
  .text 0x10000 : { *(.text) }
  .MMIX.reg_contents : { QUAD(0); }
}
