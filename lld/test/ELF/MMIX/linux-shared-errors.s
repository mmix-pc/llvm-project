# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/good.s -o %t/good.o
# RUN: not ld.lld -m elf64mmix_linux -shared -z rel %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELA
# RUN: not ld.lld -m elf64mmix_linux -shared --pack-dyn-relocs=relr %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELA
# RUN: not ld.lld -m elf64mmix_linux -shared --pack-dyn-relocs=android %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELA
# RUN: not ld.lld -m elf64mmix_linux -shared -z notext %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=TEXT
# RUN: not ld.lld -m elf64mmix_linux -shared -Bsymbolic %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=SEMANTICS
# RUN: not ld.lld -m elf64mmix_linux -shared --filter=other.so %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=SEMANTICS
# RUN: not ld.lld -m elf64mmix_linux -shared -z origin %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=SEMANTICS
# RUN: not ld.lld -m elf64mmix_linux -shared -e 0 %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ENTRY
# RUN: not ld.lld -m elf64mmix_linux -shared --version-script=%t/versions %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=VERSION
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/readonly.s -o %t/readonly.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/readonly.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=READONLY
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/tls.s -o %t/tls.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/tls.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=TLS
# RUN: yaml2obj -D SIZE=8 %t/relocs.yaml -o %t/duplicate.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/duplicate.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUPLICATE
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/unaligned.s -o %t/unaligned.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/unaligned.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ALIGN
# RUN: yaml2obj -D SIZE=4 %t/relocs.yaml -o %t/truncated.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/truncated.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDS
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/direct.s -o %t/direct.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/direct.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DIRECT
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/negative.s -o %t/negative.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/negative.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ADDEND
# RUN: not ld.lld -m elf64mmix_linux -shared --section-start=.data=0 %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=HEADER
# RUN: not ld.lld -m elf64mmix_linux --section-start=.data=0x10000 %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=HEADER
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/rx.ld %t/good.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=WRITABLE
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/narrow.s -o %t/narrow.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/narrow.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=NARROW
# RELA: require unpacked RELA relocations
# TEXT: does not support text relocations
# SEMANTICS: requests unsupported dynamic semantics
# ENTRY: entry is not in a loaded executable section
# VERSION: does not support symbol versions
# READONLY: relocation R_MMIX_64 cannot be used against
# TLS: relocation R_MMIX_64 against TLS symbol tls is unsupported
# DUPLICATE: overlapping MMIX Linux dynamic relocation targets
# ALIGN: MMIX dynamic relocation target is not 8-byte aligned
# BOUNDS: extends past the end of the section
# DIRECT: cannot bind directly to preemptible symbol
# ADDEND: invalid MMIX relative relocation symbol or addend
# HEADER: overlaps ELF or program headers
# WRITABLE: writable non-executable PT_LOAD target
# NARROW: relocation R_MMIX_32 cannot be used against

#--- good.s
.text
.globl _start
_start: SWYM 0,0,0
.data
.p2align 3
.globl object
object: .quad object
#--- versions
VERSION { global: object; };
#--- readonly.s
.section .rodata,"a",@progbits
.quad external
#--- narrow.s
.data
.long external
#--- tls.s
.section .tdata,"awT",@progbits
.type tls,@tls_object
tls: .quad 1
.data
.quad tls
#--- relocs.yaml
--- !ELF
FileHeader:
  Class: ELFCLASS64
  Data: ELFDATA2MSB
  Type: ET_REL
  Machine: EM_MMIX
Sections:
  - Name: .data
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_WRITE ]
    AddressAlign: 8
    Size: [[SIZE]]
  - Name: .rela.data
    Type: SHT_RELA
    Info: .data
    Link: .symtab
    Relocations:
      - Offset: 0
        Type: R_MMIX_64
        Symbol: external
      - Offset: 0
        Type: R_MMIX_64
        Symbol: external
Symbols:
  - Name: external
    Binding: STB_GLOBAL
#--- unaligned.s
.data
.p2align 3
.byte 0
.quad external
#--- direct.s
.text
PUSHJ r31,external
#--- negative.s
.data
.p2align 3
local: .quad local - 0x7fffffffffffffff
#--- rx.ld
PHDRS {
  headers PT_LOAD FILEHDR PHDRS FLAGS(4);
  data PT_LOAD FLAGS(7);
  dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) } :headers
  .gnu.hash : { *(.gnu.hash) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .rela.dyn : { *(.rela.dyn) }
  . = ALIGN(8192);
  .text : { *(.text) } :data
  .data : { *(.data) }
  .dynamic : { *(.dynamic) } :dynamic :data
}
