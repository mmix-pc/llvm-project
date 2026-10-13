; REQUIRES: mmix
; RUN: llvm-as %s -o %t.bc
; RUN: ld.lld -m elf64mmix_linux -shared --lto-O0 --save-temps %t.bc -o %t.o0.so
; RUN: llvm-dis %t.o0.so.0.5.precodegen.bc -o - | FileCheck %s --check-prefix=IR
; RUN: ld.lld -m elf64mmix_linux -shared --lto-O2 --save-temps %t.bc -o %t.o2.so
; RUN: llvm-dis %t.o2.so.0.5.precodegen.bc -o - | FileCheck %s --check-prefix=IR
; RUN: llvm-readobj -r %t.o0.so %t.o2.so | FileCheck %s --check-prefix=RELOC
; RUN: not ld.lld -m elf64mmix_linux -r %t.bc -o /dev/null 2>&1 | FileCheck %s --check-prefix=PARTIAL
; RUN: not ld.lld -m elf64mmix -shared %t.bc -o /dev/null 2>&1 | FileCheck %s --check-prefix=GENERIC
; IR: define {{.*}}fastcc i64 @helper(
; IR: call fastcc i64 @helper(
; IR: call i64 @external(
; RELOC-COUNT-2: R_MMIX_GLOB_DAT external 0x0
; PARTIAL: MMIX does not support relocatable links with bitcode input
; GENERIC: MMIX does not support shared object output

target datalayout = "E-m:e-p:64:64-i64:64-n64-S64"
target triple = "mmix-unknown-linux"

define hidden fastcc i64 @helper(i64 %x) noinline nounwind {
  %y = add i64 %x, 1
  ret i64 %y
}

declare i64 @external(i64) nounwind

define i64 @entry(i64 %x) nounwind {
  %a = call fastcc i64 @helper(i64 %x)
  %b = call i64 @external(i64 %a)
  ret i64 %b
}

!llvm.module.flags = !{!0}
!0 = !{i32 8, !"PIC Level", i32 2}
