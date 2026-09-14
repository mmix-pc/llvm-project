# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux %t/input.o -o %t/default
# RUN: llvm-readobj --file-headers --program-headers %t/default | FileCheck %s --check-prefix=DEFAULT
# RUN: ld.lld -m elf64mmix_linux -z max-page-size=16384 -z common-page-size=16384 %t/input.o -o %t/larger
# RUN: not ld.lld -m elf64mmix_linux -z max-page-size=4096 %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=PAGE
# RUN: not ld.lld -m elf64mmix_linux -z common-page-size=4096 %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=PAGE
# RUN: not ld.lld -m elf64mmix_linux -N %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=PAGE
# RUN: not ld.lld -m elf64mmix_linux -e 3 %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=ENTRY
# RUN: not ld.lld -m elf64mmix_linux -e data %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=EXEC
# RUN: not ld.lld -m elf64mmix_linux -e missing %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=MISSING
# RUN: not ld.lld -m elf64mmix_linux -T %t/high.ld %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=HIGH
# RUN: not ld.lld -m elf64mmix_linux -T %t/overflow.ld %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=HIGH
# RUN: %python %t/set-flags.py %t/input.o %t/flags.o
# RUN: not ld.lld -m elf64mmix_linux %t/flags.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=FLAGS
# RUN: not ld.lld -m elf64mmix_linux -T %t/unmapped.ld %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=UNMAPPED
# RUN: not ld.lld -m elf64mmix_linux -T %t/permissions.ld %t/input.o -o %t/bad 2>&1 | FileCheck %s --check-prefix=PERMS
# RUN: ld.lld -m elf64mmix -e 3 -z max-page-size=4096 %t/input.o -o %t/generic
# RUN: llvm-objcopy --strip-sections %t/default %t/stripped
# RUN: llvm-readobj --file-headers --program-headers %t/stripped | FileCheck %s --check-prefix=DEFAULT

# DEFAULT: Format: elf64-mmix
# DEFAULT: Type: Executable
# DEFAULT: Machine: EM_MMIX
# DEFAULT: Type: PT_LOAD
# DEFAULT-NEXT: Offset: 0x0
# DEFAULT-NEXT: VirtualAddress: 0x10000
# DEFAULT: Alignment: 8192
# DEFAULT: Type: PT_LOAD
# DEFAULT-NEXT: Offset: 0x158
# DEFAULT-NEXT: VirtualAddress: 0x12158
# DEFAULT: PF_X
# DEFAULT: Alignment: 8192
# DEFAULT: Type: PT_LOAD
# DEFAULT-NEXT: Offset: 0x15C
# DEFAULT-NEXT: VirtualAddress: 0x1415C
# DEFAULT: FileSize: 8
# DEFAULT: MemSize: 4104
# DEFAULT: PF_W
# DEFAULT: Alignment: 8192
# PAGE: MMIX Linux requires maximum and common page sizes of at least 8192 bytes
# ENTRY: MMIX Linux entry must be a nonnegative 4-byte-aligned address
# EXEC: MMIX Linux entry is not in a loaded executable section
# MISSING: MMIX Linux requires a defined entry symbol or address
# HIGH: is outside the nonnegative user address range
# UNMAPPED: MMIX Linux allocated section .data is not in a PT_LOAD segment
# PERMS: MMIX Linux PT_LOAD permissions do not cover section .text
# FLAGS: MMIX Linux does not support ELF e_flags

#--- input.s
.text
.global _start
_start:
SWYM 0, 0, 0
.data
.global data
data:
.quad 1
.bss
.zero 4096

#--- high.ld
SECTIONS {
  .text 0x8000000000010000 : { *(.text) }
  .data : { *(.data) }
  .bss : { *(.bss) }
}

#--- unmapped.ld
PHDRS { text PT_LOAD FLAGS(5); }
SECTIONS {
  .text 0x10000 : { *(.text) } :text
  .data 0x20000 : { *(.data) } :NONE
  .bss : { *(.bss) } :NONE
}

#--- overflow.ld
SECTIONS {
  .text 0x10000 : { *(.text) }
  .data 0x20000 : { *(.data) }
  .bss 0x7ffffffffffff800 : { *(.bss) }
}

#--- set-flags.py
import pathlib
import struct
import sys

data = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
# ELF64 e_flags has no assigned MMIX bits; inject an unknown bit.
struct.pack_into(">I", data, 48, 1)
pathlib.Path(sys.argv[2]).write_bytes(data)

#--- permissions.ld
PHDRS { text PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
  .text 0x10000 : { *(.text) } :text
  .data 0x20000 : { *(.data) } :data
  .bss : { *(.bss) } :data
}
