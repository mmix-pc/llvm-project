# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/input.s -o %t/input.o
# RUN: ld.lld -m elf64mmix_linux -shared -z lazy -z nodelete --rpath=/lib/private -init init -fini fini %t/input.o -o %t/out.so
# RUN: llvm-readobj --dynamic-table --relocations %t/out.so | FileCheck %s
# RUN: ld.lld -m elf64mmix_linux -shared --disable-new-dtags --rpath=/lib/private %t/input.o -o %t/old.so
# RUN: llvm-readobj --dynamic-table %t/old.so | FileCheck %s --check-prefix=RPATH
# RUN: not ld.lld -m elf64mmix_linux -shared -z execstack %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=STACK
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/no-headers.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=HEADERS
# RUN: not ld.lld -m elf64mmix_linux -shared -T %t/wrong-dynamic.ld %t/input.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=DYNAMIC
# RUN: yaml2obj %t/relax.yaml -o %t/relax.o
# RUN: not ld.lld -m elf64mmix_linux -shared --section-start=.text=0x2000 --section-start=.far=0x1000000 %t/relax.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELAX
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/unique.s -o %t/unique.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/unique.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNIQUE
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/unwind.s -o %t/unwind.o
# RUN: not ld.lld -m elf64mmix_linux -shared %t/unwind.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNWIND
# CHECK-DAG: FLAGS {{.*}}BIND_NOW
# CHECK-DAG: FLAGS_1 {{.*}}NOW NODELETE
# CHECK-DAG: RUNPATH {{.*}}/lib/private
# CHECK-DAG: INIT_ARRAY {{.*}}0x
# CHECK-DAG: INIT_ARRAYSZ {{.*}}8
# CHECK-DAG: FINI_ARRAY {{.*}}0x
# CHECK-DAG: FINI_ARRAYSZ {{.*}}8
# CHECK-DAG: INIT {{.*}}0x
# CHECK-DAG: FINI {{.*}}0x
# CHECK-DAG: RELACOUNT {{.*}}2
# CHECK: R_MMIX_RELATIVE
# CHECK-NEXT: R_MMIX_RELATIVE
# RPATH: RPATH {{.*}}/lib/private
# STACK: require a non-executable GNU_STACK
# HEADERS: require mapped readable ELF and program headers
# DYNAMIC: PT_DYNAMIC must describe the complete dynamic table
# RELAX: cannot expand to an absolute address in a shared object
# UNIQUE: does not support GNU_UNIQUE symbols
# UNWIND: relocation R_MMIX_64 cannot be used against

#--- input.s
.text
.globl init, fini
.hidden init, fini
init: GETA r1,local
fini: POP 0,0
.section .far,"a",@progbits
.p2align 3
local: .quad 0
.section .init_array,"aw",@init_array
.p2align 3
.quad init
.section .fini_array,"aw",@fini_array
.p2align 3
.quad fini
#--- no-headers.ld
PHDRS {
  ro PT_LOAD FLAGS(4);
  rw PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
  . = 0x2000;
  .hash : { *(.hash) } :ro
  .gnu.hash : { *(.gnu.hash) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .rela.dyn : { *(.rela.dyn) }
  . = 0x8000;
  .dynamic : { *(.dynamic) } :dynamic :rw
}
#--- unique.s
.data
.globl object
.type object,@gnu_unique_object
object: .quad 0
#--- wrong-dynamic.ld
PHDRS {
  ro PT_LOAD FILEHDR PHDRS FLAGS(4);
  rw PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(4);
  stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) } :ro :dynamic
  .gnu.hash : { *(.gnu.hash) } :ro
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .rela.dyn : { *(.rela.dyn) }
  . = ALIGN(8192);
  .dynamic : { *(.dynamic) } :rw
}
#--- unwind.s
.text
.globl function
.type function,@function
function:
.cfi_startproc
POP 0,0
.cfi_endproc
#--- relax.yaml
--- !ELF
FileHeader:
  Class: ELFCLASS64
  Data: ELFDATA2MSB
  Type: ET_REL
  Machine: EM_MMIX
Sections:
  - Name: .text
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC, SHF_EXECINSTR ]
    AddressAlign: 4
    Content: F4010000FD000000FD000000FD000000
  - Name: .far
    Type: SHT_PROGBITS
    Flags: [ SHF_ALLOC ]
    AddressAlign: 8
    Size: 8
  - Name: .rela.text
    Type: SHT_RELA
    Link: .symtab
    Info: .text
    Relocations:
      - { Offset: 0, Type: R_MMIX_GETA, Symbol: far }
Symbols:
  - { Name: far, Section: .far }
