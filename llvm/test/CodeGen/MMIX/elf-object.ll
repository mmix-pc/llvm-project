; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --file-headers %t.o \
; RUN:   | FileCheck %s --check-prefix=HEADER
; RUN: llvm-readobj --sections --section-data %t.o \
; RUN:   | FileCheck %s --check-prefix=SECTIONS
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=RELOCS
; RUN: llvm-readobj --symbols %t.o \
; RUN:   | FileCheck %s --check-prefix=SYMBOLS
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o \
; RUN:   | FileCheck %s --check-prefix=DIS --implicit-check-not='<unknown>'

; HEADER:      Format: elf64-mmix
; HEADER-NEXT: Arch: mmix
; HEADER:      Magic: (7F 45 4C 46)
; HEADER-NEXT: Class: 64-bit
; HEADER-NEXT: DataEncoding: BigEndian
; HEADER-NEXT: FileVersion: 1
; HEADER-NEXT: OS/ABI: SystemV
; HEADER-NEXT: ABIVersion: 0
; HEADER:      Type: Relocatable
; HEADER-NEXT: Machine: EM_MMIX
; HEADER-NEXT: Version: 1
; HEADER-NEXT: Entry: 0x0
; HEADER-NEXT: ProgramHeaderOffset: 0x0
; HEADER:      Flags [ (0x0)
; HEADER-NEXT: ]
; HEADER:      HeaderSize: 64
; HEADER-NEXT: ProgramHeaderEntrySize: 0
; HEADER-NEXT: ProgramHeaderCount: 0
; HEADER-NEXT: SectionHeaderEntrySize: 64

; SECTIONS:      Name: .text
; SECTIONS-NEXT: Type: SHT_PROGBITS
; SECTIONS-NEXT: Flags [
; SECTIONS-NEXT:   SHF_ALLOC
; SECTIONS-NEXT:   SHF_EXECINSTR
; SECTIONS-NEXT: ]
; SECTIONS:      Size: 140
; SECTIONS:      AddressAlignment: 4
; SECTIONS-NEXT: EntrySize: 0
; SECTIONS-NEXT: SectionData (
; SECTIONS-NEXT:   0000: 40010000 F0000000 F8000000 FE1E0004
; SECTIONS-NEXT:   0010: F31FFFFE F604001E F8000000 FE1E0004
; SECTIONS-NEXT:   0020: F4FA0000 FD000000 FD000000 FD000000
; SECTIONS-NEXT:   0030: BF1FFA00 F604001E F8000000 F4E70000
; SECTIONS-NEXT:   0040: FD000000 FD000000 FD000000 F8000000
; SECTIONS-NEXT:   0050: F4E70000 FD000000 FD000000 FD000000
; SECTIONS-NEXT:   0060: F8000000 C9FAE701 33FAFA00 42FA0006
; SECTIONS-NEXT:   0070: F4E70000 FD000000 FD000000 FD000000
; SECTIONS-NEXT:   0080: F8000000 E3E70000 F8000000
; SECTIONS-NEXT: )

; SECTIONS:      Name: .rela.text
; SECTIONS-NEXT: Type: SHT_RELA
; SECTIONS:      Size: 144
; SECTIONS:      AddressAlignment: 8
; SECTIONS-NEXT: EntrySize: 24

; SECTIONS:      Name: .data24
; SECTIONS-NEXT: Type: SHT_PROGBITS
; SECTIONS-NEXT: Flags [
; SECTIONS-NEXT:   SHF_ALLOC
; SECTIONS-NEXT:   SHF_WRITE
; SECTIONS-NEXT: ]
; SECTIONS:      Size: 4
; SECTIONS:      AddressAlignment: 1
; SECTIONS-NEXT: EntrySize: 0
; SECTIONS-NEXT: SectionData (
; SECTIONS-NEXT:   0000: A5000000
; SECTIONS-NEXT: )

; SECTIONS:      Name: .rela.data24
; SECTIONS-NEXT: Type: SHT_RELA
; SECTIONS:      Size: 24
; SECTIONS:      AddressAlignment: 8
; SECTIONS-NEXT: EntrySize: 24

; SECTIONS:      Name: .data
; SECTIONS-NEXT: Type: SHT_PROGBITS
; SECTIONS-NEXT: Flags [
; SECTIONS-NEXT:   SHF_ALLOC
; SECTIONS-NEXT:   SHF_WRITE
; SECTIONS-NEXT: ]
; SECTIONS:      Size: 48
; SECTIONS:      AddressAlignment: 8
; SECTIONS-NEXT: EntrySize: 0
; SECTIONS-NEXT: SectionData (
; SECTIONS-NEXT:   0000: 11223344 55667788 00000000 00000000
; SECTIONS-NEXT:   0010: 00000000 00000000 00000000 00000000
; SECTIONS-NEXT:   0020: 00000000 00000000 00000000 00000000
; SECTIONS-NEXT: )

; SECTIONS:      Name: .rela.data
; SECTIONS-NEXT: Type: SHT_RELA
; SECTIONS:      Size: 168
; SECTIONS:      AddressAlignment: 8
; SECTIONS-NEXT: EntrySize: 24

; SECTIONS:      Name: .bss
; SECTIONS-NEXT: Type: SHT_NOBITS
; SECTIONS-NEXT: Flags [
; SECTIONS-NEXT:   SHF_ALLOC
; SECTIONS-NEXT:   SHF_WRITE
; SECTIONS-NEXT: ]
; SECTIONS:      Size: 16
; SECTIONS:      AddressAlignment: 16
; SECTIONS-NEXT: EntrySize: 0

; SECTIONS:      Name: .rodata
; SECTIONS-NEXT: Type: SHT_PROGBITS
; SECTIONS-NEXT: Flags [
; SECTIONS-NEXT:   SHF_ALLOC
; SECTIONS-NEXT: ]
; SECTIONS:      Size: 4
; SECTIONS:      AddressAlignment: 4
; SECTIONS-NEXT: EntrySize: 0
; SECTIONS-NEXT: SectionData (
; SECTIONS-NEXT:   0000: 01020304
; SECTIONS-NEXT: )

; SECTIONS:      Name: .rodata.cst8
; SECTIONS-NEXT: Type: SHT_PROGBITS
; SECTIONS-NEXT: Flags [
; SECTIONS-NEXT:   SHF_ALLOC
; SECTIONS-NEXT:   SHF_MERGE
; SECTIONS-NEXT: ]
; SECTIONS:      Size: 8
; SECTIONS:      AddressAlignment: 8
; SECTIONS-NEXT: EntrySize: 8
; SECTIONS-NEXT: SectionData (
; SECTIONS-NEXT:   0000: 01020304 05060708
; SECTIONS-NEXT: )

; The complete relocation list excludes primary expanding records,
; intermediate relaxation records, split-address records, and private LLVM
; identities by construction.
; RELOCS:      Relocations [
; RELOCS-NEXT:   Section {{.*}} .rela.text {
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x0
; RELOCS-NEXT:       Type: R_MMIX_ADDR19 (30)
; RELOCS-NEXT:       Symbol: mc_external
; RELOCS-NEXT:       Addend: 0x4
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x4
; RELOCS-NEXT:       Type: R_MMIX_ADDR27 (31)
; RELOCS-NEXT:       Symbol: mc_external
; RELOCS-NEXT:       Addend: 0xFFFFFFFFFFFFFFF8
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x20
; RELOCS-NEXT:       Type: R_MMIX_GETA (13)
; RELOCS-NEXT:       Symbol: external_function
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x3C
; RELOCS-NEXT:       Type: R_MMIX_GETA (13)
; RELOCS-NEXT:       Symbol: external_data
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x50
; RELOCS-NEXT:       Type: R_MMIX_GETA (13)
; RELOCS-NEXT:       Symbol: .rodata.cst8
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x70
; RELOCS-NEXT:       Type: R_MMIX_GETA (13)
; RELOCS-NEXT:       Symbol: .text
; RELOCS-NEXT:       Addend: 0x70
; RELOCS-NEXT:     }
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Section {{.*}} .rela.data24 {
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x0
; RELOCS-NEXT:       Type: R_MMIX_24 (3)
; RELOCS-NEXT:       Symbol: mc_external
; RELOCS-NEXT:       Addend: 0x7
; RELOCS-NEXT:     }
; RELOCS-NEXT:   }
; RELOCS-NEXT:   Section {{.*}} .rela.data {
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x8
; RELOCS-NEXT:       Type: R_MMIX_8 (1)
; RELOCS-NEXT:       Symbol: external_data
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0xA
; RELOCS-NEXT:       Type: R_MMIX_16 (2)
; RELOCS-NEXT:       Symbol: external_data
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0xC
; RELOCS-NEXT:       Type: R_MMIX_32 (4)
; RELOCS-NEXT:       Symbol: external_data
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x10
; RELOCS-NEXT:       Type: R_MMIX_64 (5)
; RELOCS-NEXT:       Symbol: external_data
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x18
; RELOCS-NEXT:       Type: R_MMIX_64 (5)
; RELOCS-NEXT:       Symbol: initialized_data
; RELOCS-NEXT:       Addend: 0x0
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x20
; RELOCS-NEXT:       Type: R_MMIX_64 (5)
; RELOCS-NEXT:       Symbol: .text
; RELOCS-NEXT:       Addend: 0x8
; RELOCS-NEXT:     }
; RELOCS-NEXT:     Relocation {
; RELOCS-NEXT:       Offset: 0x28
; RELOCS-NEXT:       Type: R_MMIX_64 (5)
; RELOCS-NEXT:       Symbol: .text
; RELOCS-NEXT:       Addend: 0x70
; RELOCS-NEXT:     }
; RELOCS-NEXT:   }
; RELOCS-NEXT: ]

; SYMBOLS:      Name: local_target
; SYMBOLS-NEXT: Value: 0x8
; SYMBOLS-NEXT: Size: 4
; SYMBOLS-NEXT: Binding: Local
; SYMBOLS-NEXT: Type: Function
; SYMBOLS:      Section: .text
; SYMBOLS:      Name: .rodata.cst8
; SYMBOLS:      Binding: Local
; SYMBOLS-NEXT: Type: Section
; SYMBOLS:      Section: .rodata.cst8
; SYMBOLS:      Name: mc_external
; SYMBOLS:      Binding: Global
; SYMBOLS-NEXT: Type: None
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: external_function
; SYMBOLS:      Binding: Global
; SYMBOLS-NEXT: Type: None
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: external_data
; SYMBOLS:      Binding: Global
; SYMBOLS-NEXT: Type: None
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: initialized_data
; SYMBOLS:      Size: 8
; SYMBOLS-NEXT: Binding: Global
; SYMBOLS-NEXT: Type: Object
; SYMBOLS:      Section: .data
; SYMBOLS:      Name: zero_storage
; SYMBOLS:      Size: 16
; SYMBOLS-NEXT: Binding: Global
; SYMBOLS-NEXT: Type: Object
; SYMBOLS:      Section: .bss
; SYMBOLS:      Name: readonly_constant
; SYMBOLS:      Size: 4
; SYMBOLS-NEXT: Binding: Global
; SYMBOLS-NEXT: Type: Object
; SYMBOLS:      Section: .rodata
; SYMBOLS:      Name: data_pointer
; SYMBOLS:      Value: 0x18
; SYMBOLS-NEXT: Size: 8
; SYMBOLS:      Section: .data
; SYMBOLS:      Name: function_pointer
; SYMBOLS:      Value: 0x20
; SYMBOLS-NEXT: Size: 8
; SYMBOLS:      Section: .data
; SYMBOLS:      Name: block_pointer
; SYMBOLS:      Value: 0x28
; SYMBOLS-NEXT: Size: 8
; SYMBOLS:      Section: .data

; DIS-LABEL: <mc_terminal>:
; DIS-NEXT:  {{.*}} BN r1, 0
; DIS-NEXT:  {{.*}} R_MMIX_ADDR19 mc_external+0x4
; DIS-NEXT:  {{.*}} JMP 0
; DIS-NEXT:  {{.*}} R_MMIX_ADDR27 mc_external-0x8
; DIS-LABEL: <local_call>:
; DIS:       {{.*}} PUSHJB r31, -2
; DIS-LABEL: <symbolic_call>:
; DIS:       {{.*}} GETA {{r[0-9]+}}, 0
; DIS-NEXT:  {{.*}} R_MMIX_GETA external_function
; DIS:       PUSHGO r31, {{r[0-9]+}}, 0
; DIS-LABEL: <external_address>:
; DIS-NEXT:  {{.*}} GETA r231, 0
; DIS-NEXT:  {{.*}} R_MMIX_GETA external_data
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-LABEL: <constant_pool_address>:
; DIS-NEXT:  {{.*}} GETA r231, 0
; DIS-NEXT:  {{.*}} R_MMIX_GETA .rodata.cst8
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-LABEL: <block_address>:
; DIS:       {{.*}} BZ r250, 6
; DIS-NEXT:  {{.*}} GETA r231, 0
; DIS-NEXT:  {{.*}} R_MMIX_GETA .text+0x70
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0
; DIS-NEXT:  {{.*}} SWYM 0, 0, 0

target triple = "mmix-unknown-elf"

; These records have reviewed MC semantics but no ordinary IR producer.
module asm ".section .data24,\22aw\22,@progbits"
module asm ".global mc_external"
module asm ".mmix_24 165, mc_external + 7"
module asm ".text"
module asm "mc_terminal:"
module asm "BN r1, mc_external + 4"
module asm "JMP mc_external - 8"

@initialized_data = global i64 1234605616436508552, align 8
@zero_storage = global [16 x i8] zeroinitializer, align 16
@readonly_constant = constant i32 16909060, align 4
; This mergeable entry composes the object contract whose machine
; constant-pool producer is covered separately by elf-constant-pool-address.mir.
@constant_pool_entry = private unnamed_addr constant i64 72623859790382856,
    align 8

@external_data = external global i8

@external_i8 = global i8 ptrtoint (ptr @external_data to i8), align 1
@external_i16 = global i16 ptrtoint (ptr @external_data to i16), align 2
@external_i32 = global i32 ptrtoint (ptr @external_data to i32), align 4
@external_i64 = global i64 ptrtoint (ptr @external_data to i64), align 8
@data_pointer = global ptr @initialized_data, align 8
@function_pointer = global ptr @local_target, align 8
@block_pointer = global ptr blockaddress(@block_address, %taken), align 8

declare void @external_function()

define internal void @local_target() nounwind {
  ret void
}

define void @local_call() nounwind {
  call void @local_target()
  ret void
}

define void @symbolic_call() nounwind {
  call void @external_function()
  ret void
}

define ptr @external_address() nounwind {
  ret ptr @external_data
}

define ptr @constant_pool_address() nounwind {
  ret ptr @constant_pool_entry
}

define ptr @block_address(i1 %condition) nounwind {
entry:
  br i1 %condition, label %taken, label %other

taken:
  ret ptr blockaddress(@block_address, %taken)

other:
  ret ptr null
}
