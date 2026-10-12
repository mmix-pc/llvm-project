; REQUIRES: mmix
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -filetype=obj %s -o %t.o
; RUN: ld.lld -m elf64mmix_linux --emit-relocs %t.o -o %t
; RUN: llvm-readobj --relocations %t | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -filetype=obj %s -o %t.o
; RUN: ld.lld -m elf64mmix_linux --emit-relocs %t.o -o %t
; RUN: llvm-readobj --relocations %t | FileCheck %s

; Retained relocation records identify both eager function and data GOT paths
; after a real final link. This is not a dynamic loader execution test.
; CHECK: .rela.text {
; CHECK-DAG: R_MMIX_GOT_PC64 callee 0x0
; CHECK-DAG: R_MMIX_GOT_PC64 object 0x0
; CHECK: }

@object = global i64 42, align 8

define ptr @_start() {
  %p = call ptr @callee()
  ret ptr %p
}

define ptr @callee() {
  ret ptr @object
}
