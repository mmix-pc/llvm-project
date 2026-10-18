; REQUIRES: mmix
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -filetype=obj %s -o %t.o
; RUN: ld.lld -m elf64mmix_linux -shared %t.o -o %t.so
; RUN: llvm-readobj --relocations --hex-dump=.got %t.so | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -filetype=obj %s -o %t.o
; RUN: ld.lld -m elf64mmix_linux -shared %t.o -o %t.so
; RUN: llvm-readobj --relocations --hex-dump=.got %t.so | FileCheck %s

; Undefined hidden weak symbols use zero-valued slots without load-bias
; relocations. PC-relative references to absolute zero are not nullable.
; CHECK: Relocations [
; CHECK-NEXT: ]
; CHECK: Hex dump of section '.got':
; CHECK-NEXT: 0x{{[0-9a-f]+}} 00000000 00000000 00000000 00000000

@object = extern_weak hidden global i8
declare extern_weak hidden void @function()

define ptr @object_address() {
  ret ptr @object
}

define ptr @function_address() {
  ret ptr @function
}
