# REQUIRES: mmix
# RUN: split-file %s %t
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
# RUN: ld.lld -m elf64mmix_linux -shared --soname=libversions.so --version-script=%t/versions %t/provider.o -o %t/provider.so
# RUN: llvm-readelf --version-info --dyn-syms %t/provider.so | FileCheck %s --check-prefix=DEF
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/consumer.s -o %t/consumer.o
# RUN: ld.lld -m elf64mmix_linux -shared -z defs %t/consumer.o %t/provider.so -o %t/consumer.so
# RUN: llvm-readelf --version-info --dyn-syms --dynamic %t/consumer.so | FileCheck %s --check-prefix=NEED
# RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/missing.s -o %t/missing.o
# RUN: not ld.lld -m elf64mmix_linux -shared -z defs %t/missing.o %t/provider.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
# RUN: not ld.lld -m elf64mmix_linux -shared %t/provider.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNDECLARED
# RUN: llvm-objcopy --dump-section=.gnu.version=%t/version-bytes %t/provider.so
# RUN: %python -c "from pathlib import Path; p=Path(r'%t/version-bytes'); b=bytearray(p.read_bytes()); b[-2:]=bytes([127,254]); p.write_bytes(b)"
# RUN: llvm-objcopy --update-section=.gnu.version=%t/version-bytes %t/provider.so %t/corrupt.so
# RUN: not ld.lld -m elf64mmix_linux -shared %t/consumer.o %t/corrupt.so -o /dev/null 2>&1 | FileCheck %s --check-prefix=CORRUPT
# RUN: not ld.lld -m elf64mmix_linux -shared --version-script=%t/versions -T %t/writable.ld %t/provider.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=WRITABLE
# DEF-DAG: foo@V1
# DEF-DAG: foo@@V2
# DEF-DAG: bar@@V1
# DEF-DAG: Version definition section
# DEF-DAG: Name: V1
# DEF-DAG: Name: V2
# NEED-DAG: (NEEDED) {{.*}}libversions.so
# NEED-DAG: (VERSYM)
# NEED-DAG: (VERNEED)
# NEED-DAG: foo@V1
# NEED-DAG: foo@V2
# NEED-DAG: File: libversions.so
# NEED-DAG: Name: V1
# NEED-DAG: Name: V2
# MISSING: undefined symbol: foo@V3
# UNDECLARED: symbol foo@V1 has undefined version V1
# CORRUPT: version definition index 32766
# CORRUPT-SAME: out of bounds
# WRITABLE: MMIX symbol versions require read-only file-backed PT_LOAD storage

#--- versions
V1 { global: foo; bar; local: *; };
V2 { global: foo; } V1;
#--- provider.s
.text
.globl old, current, bar
.type old,@function
old: SWYM 0,0,0
.type current,@function
current: SWYM 0,0,0
.type bar,@function
bar: SWYM 0,0,0
.symver old,foo@V1
.symver current,foo@@V2
#--- consumer.s
.data
.p2align 3
.quad old
.quad foo
.symver old,foo@V1
#--- missing.s
.data
.p2align 3
.quad missing
.symver missing,foo@V3
#--- writable.ld
PHDRS {
  headers PT_LOAD FILEHDR PHDRS FLAGS(4);
  versions PT_LOAD FLAGS(6);
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) } :headers
  .gnu.hash : { *(.gnu.hash) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  . = ALIGN(8192);
  .gnu.version : { *(.gnu.version) } :versions
  .gnu.version_d : { *(.gnu.version_d) }
  . = ALIGN(8192);
  .text : { *(.text) } :text
  . = ALIGN(8192);
  .dynamic : { *(.dynamic) } :data :dynamic
}
