; REQUIRES: mmix
; RUN: llc -mtriple=mmix-unknown-linux -O2 -function-sections -filetype=obj %s -o %t.o
; RUN: ld.lld -m elf64mmix_linux -static --gc-sections -e entry %t.o -o %t.static
; RUN: llvm-dwarfdump --eh-frame %t.static | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -relocation-model=pic -function-sections -filetype=obj %s -o %t.pic.o
; RUN: ld.lld -m elf64mmix_linux -pie --dynamic-linker=/lib/ld-llvm-mmix.so.1 --gc-sections -e entry %t.pic.o -o %t.pie
; RUN: llvm-dwarfdump --eh-frame %t.pie | FileCheck %s
; RUN: ld.lld -m elf64mmix_linux -shared --gc-sections -soname libframes.so %t.pic.o -o %t.so
; RUN: llvm-dwarfdump --eh-frame %t.so | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: ld.lld -m elf64mmix_linux -static --gc-sections --lto-O2 -e entry %t.bc -o %t.lto
; RUN: llvm-dwarfdump --eh-frame %t.lto | FileCheck %s

; CHECK: Return address column: 35
; CHECK: FDE cie=
; CHECK: DW_CFA_def_cfa: R254 +0
; CHECK: DW_CFA_same_value: RJ
; CHECK: FDE cie=
; CHECK: DW_CFA_def_cfa: R254 +0
; CHECK-NOT: FDE cie=

target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"

@value = internal global i64 11, align 8

define i64 @leaf() noinline uwtable(async) {
  %v = load volatile i64, ptr @value
  ret i64 %v
}

define i64 @entry() noinline uwtable {
  %v = call i64 @leaf()
  %r = add i64 %v, 1
  ret i64 %r
}

define internal void @unreferenced() noinline uwtable(async) {
  ret void
}

!llvm.module.flags = !{!0}
!0 = !{i32 7, !"uwtable", i32 2}
