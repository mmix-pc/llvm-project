# REQUIRES: mmix

# RUN: echo 'SECTIONS { .text 0x1000 : { *(.text) } \
# RUN:                    .data 0x2000 : { *(.data) } }' > %t.script
# RUN: yaml2obj --docnum=1 %s -o %t.o
# RUN: ld.lld -e 0 -T %t.script %t.o -o %t
# RUN: llvm-objdump -s --section=.data %t | FileCheck %s --check-prefix=DATA
# RUN: ld.lld -m elf64mmix_linux -e 0 -T %t.script %t.o -o %t.linux
# RUN: llvm-objdump -s --section=.data %t.linux | FileCheck %s --check-prefix=DATA
# RUN: yaml2obj --docnum=2 %s -o %t-overflow.o
# RUN: not ld.lld -e 0 -T %t.script %t-overflow.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OVERFLOW
# RUN: not ld.lld -m elf64mmix_linux -e 0 -T %t.script %t-overflow.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OVERFLOW
# RUN: yaml2obj --docnum=3 %s -o %t-malformed.o
# RUN: not ld.lld -e 0 -T %t.script %t-malformed.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MALFORMED
# RUN: yaml2obj --docnum=4 %s -o %t-rel.o
# RUN: not ld.lld -e 0 -T %t.script %t-rel.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=REL

## R_MMIX_NONE leaves DEADBEEF unchanged. The remaining fields exercise
## S+A, RELA-only addends, each symbol binding, both bitfield boundaries,
## big-endian storage, 24-in-32 high-byte preservation, and 64-bit wrap.
# DATA:      Contents of section .data:
# DATA-NEXT: 2000 deadbeef 400054a5 00016b89 abcdef00
# DATA-NEXT: 2010 00000000 000fffff ff00ffff 00005aff
# DATA-NEXT: 2020 ffff7e00 0000ffff ffff0000 00000000
# DATA-NEXT: 2030 00000000 0000

# OVERFLOW-DAG: relocation R_MMIX_8 out of range: 256 is not in [-256, 255]
# OVERFLOW-DAG: relocation R_MMIX_8 out of range: -257 is not in [-256, 255]
# OVERFLOW-DAG: relocation R_MMIX_16 out of range: 65536 is not in [-65536, 65535]
# OVERFLOW-DAG: relocation R_MMIX_16 out of range: -65537 is not in [-65536, 65535]
# OVERFLOW-DAG: relocation R_MMIX_24 out of range: 16777216 is not in [-16777216, 16777215]
# OVERFLOW-DAG: relocation R_MMIX_24 out of range: -16777217 is not in [-16777216, 16777215]
# OVERFLOW-DAG: relocation R_MMIX_32 out of range: 4294967296 is not in [-4294967296, 4294967295]
# OVERFLOW-DAG: relocation R_MMIX_32 out of range: -4294967297 is not in [-4294967296, 4294967295]

# MALFORMED-DAG: relocation R_MMIX_8 offset 8 is outside the section
# MALFORMED-DAG: relocation R_MMIX_16 field at offset 7 extends past the end of the section
# MALFORMED-DAG: relocation R_MMIX_24 field at offset 5 extends past the end of the section
# MALFORMED-DAG: relocation R_MMIX_64 field at offset 1 extends past the end of the section
# REL: MMIX supports only RELA relocations; R_MMIX_64 has no explicit addend

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2MSB
  Type:    ET_REL
  Machine: EM_MMIX
Sections:
  - Name:         .text
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content:      FD000000
  - Name:         .data
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Content:      DEADBEEF000000A5FFFFFF000000000000000000000000000000000000005AFFFFFF7EFFFFFF00000000000000000000000000000000
  - Name: .rela.data
    Type: SHT_RELA
    Link: .symtab
    Info: .data
    Relocations:
      - { Offset: 0,  Type: R_MMIX_NONE, Symbol: global_abs32, Addend: 1 }
      - { Offset: 4,  Type: R_MMIX_8,  Symbol: local_abs, Addend: 0 }
      - { Offset: 5,  Type: R_MMIX_16, Symbol: global_abs16, Addend: 16 }
      - { Offset: 7,  Type: R_MMIX_24, Symbol: weak_abs24, Addend: 291 }
      - { Offset: 11, Type: R_MMIX_32, Symbol: global_abs32, Addend: 0 }
      - { Offset: 15, Type: R_MMIX_64, Symbol: text_section, Addend: -1 }
      - { Offset: 23, Type: R_MMIX_8,  Symbol: weak_undefined, Addend: -1 }
      - { Offset: 24, Type: R_MMIX_8,  Symbol: abs_u8, Addend: 0 }
      - { Offset: 25, Type: R_MMIX_8,  Symbol: abs_s8, Addend: 0 }
      - { Offset: 26, Type: R_MMIX_16, Symbol: abs_u16, Addend: 0 }
      - { Offset: 28, Type: R_MMIX_16, Symbol: abs_s16, Addend: 0 }
      - { Offset: 30, Type: R_MMIX_24, Symbol: abs_u24, Addend: 0 }
      - { Offset: 34, Type: R_MMIX_24, Symbol: abs_s24, Addend: 0 }
      - { Offset: 38, Type: R_MMIX_32, Symbol: abs_u32, Addend: 0 }
      - { Offset: 42, Type: R_MMIX_32, Symbol: abs_s32, Addend: 0 }
      - { Offset: 46, Type: R_MMIX_64, Symbol: abs_wrap, Addend: 1 }
Symbols:
  - { Name: local_abs,    Index: SHN_ABS, Value: 0x40 }
  - { Name: text_section, Type: STT_SECTION, Section: .text }
  - { Name: global_abs16, Index: SHN_ABS, Value: 0x44, Binding: STB_GLOBAL }
  - { Name: weak_abs24,   Index: SHN_ABS, Value: 0x48, Binding: STB_WEAK }
  - { Name: global_abs32, Index: SHN_ABS, Value: 0x89abcdef, Binding: STB_GLOBAL }
  - { Name: weak_undefined, Binding: STB_WEAK }
  - { Name: abs_u8,  Index: SHN_ABS, Value: 0xff, Binding: STB_GLOBAL }
  - { Name: abs_s8,  Index: SHN_ABS, Value: 0xffffffffffffff00, Binding: STB_GLOBAL }
  - { Name: abs_u16, Index: SHN_ABS, Value: 0xffff, Binding: STB_GLOBAL }
  - { Name: abs_s16, Index: SHN_ABS, Value: 0xffffffffffff0000, Binding: STB_GLOBAL }
  - { Name: abs_u24, Index: SHN_ABS, Value: 0xffffff, Binding: STB_GLOBAL }
  - { Name: abs_s24, Index: SHN_ABS, Value: 0xffffffffff000000, Binding: STB_GLOBAL }
  - { Name: abs_u32, Index: SHN_ABS, Value: 0xffffffff, Binding: STB_GLOBAL }
  - { Name: abs_s32, Index: SHN_ABS, Value: 0xffffffff00000000, Binding: STB_GLOBAL }
  - { Name: abs_wrap, Index: SHN_ABS, Value: 0xffffffffffffffff, Binding: STB_GLOBAL }

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2MSB
  Type:    ET_REL
  Machine: EM_MMIX
Sections:
  - Name:         .text
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content:      FD000000
  - Name:         .data
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Content:      00000000000000000000000000000000000000000000
  - Name: .rela.data
    Type: SHT_RELA
    Link: .symtab
    Info: .data
    Relocations:
      - { Offset: 0,  Type: R_MMIX_8,  Symbol: pos8, Addend: 0 }
      - { Offset: 1,  Type: R_MMIX_8,  Symbol: neg8, Addend: 0 }
      - { Offset: 2,  Type: R_MMIX_16, Symbol: pos16, Addend: 0 }
      - { Offset: 4,  Type: R_MMIX_16, Symbol: neg16, Addend: 0 }
      - { Offset: 6,  Type: R_MMIX_24, Symbol: pos24, Addend: 0 }
      - { Offset: 10, Type: R_MMIX_24, Symbol: neg24, Addend: 0 }
      - { Offset: 14, Type: R_MMIX_32, Symbol: pos32, Addend: 0 }
      - { Offset: 18, Type: R_MMIX_32, Symbol: neg32, Addend: 0 }
Symbols:
  - { Name: pos8,  Index: SHN_ABS, Value: 0x100 }
  - { Name: neg8,  Index: SHN_ABS, Value: 0xfffffffffffffeff }
  - { Name: pos16, Index: SHN_ABS, Value: 0x10000 }
  - { Name: neg16, Index: SHN_ABS, Value: 0xfffffffffffeffff }
  - { Name: pos24, Index: SHN_ABS, Value: 0x1000000 }
  - { Name: neg24, Index: SHN_ABS, Value: 0xfffffffffeffffff }
  - { Name: pos32, Index: SHN_ABS, Value: 0x100000000 }
  - { Name: neg32, Index: SHN_ABS, Value: 0xfffffffeffffffff }

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2MSB
  Type:    ET_REL
  Machine: EM_MMIX
Sections:
  - Name:         .text
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content:      FD000000
  - Name:         .data
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 1
    Content:      0000000000000000
  - Name: .rela.data
    Type: SHT_RELA
    Link: .symtab
    Info: .data
    Relocations:
      - { Offset: 8, Type: R_MMIX_8,  Symbol: zero, Addend: 0 }
      - { Offset: 7, Type: R_MMIX_16, Symbol: zero, Addend: 0 }
      - { Offset: 5, Type: R_MMIX_24, Symbol: zero, Addend: 0 }
      - { Offset: 1, Type: R_MMIX_64, Symbol: zero, Addend: 0 }
Symbols:
  - { Name: zero, Index: SHN_ABS, Value: 0 }

--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2MSB
  Type:    ET_REL
  Machine: EM_MMIX
Sections:
  - Name:         .text
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content:      FD000000
  - Name:         .data
    Type:         SHT_PROGBITS
    Flags:        [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 8
    Content:      0000000000000000
  - Name: .rel.data
    Type: SHT_REL
    Link: .symtab
    Info: .data
    Relocations:
      - { Offset: 0, Type: R_MMIX_64, Symbol: zero }
Symbols:
  - { Name: zero, Index: SHN_ABS, Value: 0 }
