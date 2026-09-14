# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix -filetype=obj %t/start.s -o %t/start.o
# RUN: ld.lld -m elf64mmix_linux %t/start.o -o %t/linux
# RUN: llvm-readobj --file-headers %t/linux | FileCheck %s --check-prefix=HEADER
# RUN: ld.lld -m elf64mmix_linux -r %t/start.o -o %t/partial.o
# RUN: ld.lld -m elf64mmix_linux %t/partial.o -o %t/relinked
# RUN: llvm-mc -triple=mmix -filetype=obj %t/greg.s -o %t/greg.o
# RUN: not ld.lld -m elf64mmix_linux %t/greg.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=GREG
# RUN: not ld.lld -m elf64mmix_linux -r %t/greg.o -o %t/bad.o 2>&1 | FileCheck %s --check-prefix=GREG
# RUN: not ld.lld -m elf64mmix_linux -T %t/format.ld %t/greg.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=GREG
## Direct emulation selection retains the usual last-option-wins behavior.
# RUN: ld.lld -m elf64mmix_linux -m elf64mmix %t/greg.o -o %t/generic
# RUN: not ld.lld -m elf64mmix -m elf64mmix_linux %t/greg.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=GREG
# RUN: ld.lld %t/greg.o -o %t/inferred
# RUN: yaml2obj %t/base.yaml -o %t/base.o
# RUN: not ld.lld -m elf64mmix_linux %t/start.o %t/base.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: not ld.lld -m elf64mmix_linux -r %t/base.o -o %t/bad.o 2>&1 | FileCheck %s --check-prefix=BASE
# RUN: ld.lld -m elf64mmix %t/start.o %t/base.o -o %t/base
# RUN: llvm-as %t/input.ll -o %t/input.bc
# RUN: not ld.lld -m elf64mmix_linux %t/input.bc -o %t/bad 2>&1 | FileCheck %s --check-prefix=BITCODE
# RUN: not ld.lld -m elf64mmix_linux -shared %t/start.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=SHARED
# RUN: not ld.lld -m elf64mmix_linux -pie %t/start.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=PIE
# RUN: yaml2obj -DREG=229 %t/local.yaml -o %t/local.o
# RUN: ld.lld -m elf64mmix_linux %t/start.o %t/local.o -o %t/local
# RUN: yaml2obj -DREG=230 %t/local.yaml -o %t/global.o
# RUN: not ld.lld -m elf64mmix_linux %t/start.o %t/global.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=LOCAL
# RUN: ld.lld -m elf64mmix %t/start.o %t/global.o -o %t/generic-local
# RUN: not ld.lld -m elf64mmixlinux %t/start.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=ALIAS
# HEADER: Format: elf64-mmix
# HEADER: OS/ABI: SystemV
# HEADER: Type: Executable
# GREG: MMIX Linux does not support loader-initialized register contents
# BASE: MMIX Linux does not support R_MMIX_BASE_PLUS_OFFSET requiring loader-initialized global registers
# BITCODE: MMIX Linux does not support bitcode input
# SHARED: MMIX does not support shared object output
# PIE: MMIX does not support PIE output
# LOCAL: R_MMIX_LOCAL register $230 is not local; first global register is $230
# ALIAS: unknown emulation: elf64mmixlinux

#--- start.s
.global _start
_start:
SWYM 0, 0, 0

#--- greg.s
.global _start
_start:
SWYM 0, 0, 0
.section .MMIX.reg_contents,"",@progbits
.quad 0

#--- format.ld
OUTPUT_FORMAT(elf64-mmix)

#--- base.yaml
--- !ELF
FileHeader: { Class: ELFCLASS64, Data: ELFDATA2MSB, Type: ET_REL, Machine: EM_MMIX }
Sections:
  - Name: .text
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content: 23AA0000
  - Name: .rela.text
    Type: SHT_RELA
    Link: .symtab
    Info: .text
    Relocations:
      - { Offset: 2, Type: R_MMIX_BASE_PLUS_OFFSET, Symbol: address }
Symbols:
  - { Name: address, Index: SHN_ABS, Value: 256 }

#--- local.yaml
--- !ELF
FileHeader: { Class: ELFCLASS64, Data: ELFDATA2MSB, Type: ET_REL, Machine: EM_MMIX }
Sections:
  - Name: .text
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content: FD000000
  - Name: .rela.text
    Type: SHT_RELA
    Link: .symtab
    Info: .text
    Relocations:
      - { Offset: 0, Type: R_MMIX_LOCAL, Symbol: reg }
Symbols:
  - { Name: reg, Index: SHN_ABS, Value: '[[REG]]' }

#--- input.ll
target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"
define void @_start() {
  ret void
}
