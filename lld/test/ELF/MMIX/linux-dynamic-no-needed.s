# REQUIRES: mmix
# RUN: llvm-mc -triple=mmix -filetype=obj %s -o %t.o
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 %t.o -o %t.exec
# RUN: llvm-readobj -h -l -d %t.exec | FileCheck %s --check-prefix=DYNAMIC --implicit-check-not=NEEDED
# RUN: ld.lld -m elf64mmix_linux --dynamic-linker=/lib/ld-llvm-mmix.so.1 --export-dynamic %t.o -o %t.export
# RUN: llvm-readobj -h -l -d %t.export | FileCheck %s --check-prefix=DYNAMIC --implicit-check-not=NEEDED
# RUN: llvm-readobj --dyn-symbols %t.export | FileCheck %s --check-prefix=EXPORT
# RUN: llvm-readobj --dyn-symbols %t.exec | FileCheck %s --check-prefix=NOEXPORT
# RUN: ld.lld -m elf64mmix_linux --no-dynamic-linker --export-dynamic %t.o -o %t.static
# RUN: llvm-readobj -h -l -d %t.static | FileCheck %s --check-prefix=STATIC --implicit-check-not=PT_DYNAMIC --implicit-check-not=PT_INTERP
# RUN: ld.lld -m elf64mmix_linux -r %t.o -o %t.rel
# RUN: llvm-readobj -h -l -d %t.rel | FileCheck %s --check-prefix=REL --implicit-check-not=PT_DYNAMIC --implicit-check-not=PT_INTERP
# DYNAMIC: Type: Executable
# DYNAMIC-DAG: Type: PT_INTERP
# DYNAMIC-DAG: Type: PT_DYNAMIC
# DYNAMIC: DynamicSection [
# DYNAMIC-DAG: GNU_HASH
# DYNAMIC-DAG: HASH
# DYNAMIC-DAG: STRTAB
# DYNAMIC-DAG: SYMTAB
# DYNAMIC-DAG: BIND_NOW
# DYNAMIC: NULL
# EXPORT: Name: exported (
# NOEXPORT: DynamicSymbols [
# NOEXPORT-NOT: Name: exported (
# STATIC: Type: Executable
# REL: Type: Relocatable

.text
.globl _start
_start: SWYM 0,0,0
.data
.p2align 3
.globl exported
exported: .quad 42
