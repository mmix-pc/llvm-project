# REQUIRES: mmix

# RUN: yaml2obj %s -o %t.o
# RUN: not ld.lld --error-limit=0 -e 0 %t.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not='unsupported relocation Unknown'

## GNU continuation records require their primary sequence context.
# CHECK-DAG: unsupported relocation R_MMIX_GETA_1 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_GETA_2 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_GETA_3 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_CBRANCH_J against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_CBRANCH_1 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_CBRANCH_2 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_CBRANCH_3 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_PUSHJ_1 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_PUSHJ_2 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_PUSHJ_3 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_JMP_1 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_JMP_2 against symbol target: GNU relaxation continuation cannot be used as standalone input
# CHECK-DAG: unsupported relocation R_MMIX_JMP_3 against symbol target: GNU relaxation continuation cannot be used as standalone input

## GNU vtable metadata records remain recognized but unimplemented.
# CHECK-DAG: unsupported relocation R_MMIX_GNU_VTINHERIT against symbol target: requires GNU vtable metadata support
# CHECK-DAG: unsupported relocation R_MMIX_GNU_VTENTRY against symbol target: requires GNU vtable metadata support

## Values outside the GNU MMIX enum and Linux TLS extensions are unknown.
# CHECK-DAG: unknown relocation (41) against symbol target

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
    Content:      F4010000FD000000FD000000FD00000040010000FD000000FD000000FD000000FD000000FD000000F2010000FD000000FD000000FD000000FD000000F0000000FD000000FD000000FD000000FD000000FD000000
  - Name: .rela.text
    Type: SHT_RELA
    Link: .symtab
    Info: .text
    Relocations:
      - { Offset: 0, Type: R_MMIX_GNU_VTINHERIT, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_GNU_VTENTRY, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_GETA, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_GETA_1, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_GETA_2, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_GETA_3, Symbol: target, Addend: 0 }
      - { Offset: 16, Type: R_MMIX_CBRANCH, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_CBRANCH_J, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_CBRANCH_1, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_CBRANCH_2, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_CBRANCH_3, Symbol: target, Addend: 0 }
      - { Offset: 40, Type: R_MMIX_PUSHJ, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_PUSHJ_1, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_PUSHJ_2, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_PUSHJ_3, Symbol: target, Addend: 0 }
      - { Offset: 60, Type: R_MMIX_JMP, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_JMP_1, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_JMP_2, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: R_MMIX_JMP_3, Symbol: target, Addend: 0 }
      - { Offset: 0, Type: 41, Symbol: target, Addend: 0 }
Symbols:
  - Name:    target
    Section: .text
    Value:   80
    Binding: STB_GLOBAL
