; REQUIRES: mmix
; RUN: split-file %s %t
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
; RUN: ld.lld -m elf64mmix_linux -shared -soname libtls.so %t/provider.o -o %t/libtls.so
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -filetype=obj %t/access.ll -o %t/o0.o
; RUN: ld.lld -m elf64mmix_linux -shared %t/o0.o %t/libtls.so -o %t/o0.so
; RUN: llvm-readobj -r %t/o0.so | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -filetype=obj %t/access.ll -o %t/o2.o
; RUN: ld.lld -m elf64mmix_linux -shared %t/o2.o %t/libtls.so -o %t/o2.so
; RUN: llvm-readobj -r %t/o2.so | FileCheck %s
; CHECK: Section {{.*}} .rela.dyn {
; CHECK-DAG: R_MMIX_TPOFF64 ie 0x0
; CHECK-DAG: R_MMIX_DTPMOD64 gd 0x0
; CHECK-DAG: R_MMIX_DTPOFF64 gd 0x0
; CHECK-DAG: R_MMIX_DTPMOD64 - 0x0
; CHECK-DAG: R_MMIX_GLOB_DAT __tls_get_addr 0x0
; CHECK: }

;--- provider.s
.text
.globl __tls_get_addr
.type __tls_get_addr,@function
__tls_get_addr: POP 0,0
.section .tdata,"awT",@progbits
.p2align 3
.globl ie, gd
.type ie,@tls_object
.type gd,@tls_object
ie: .quad 1
gd: .quad 2

;--- access.ll
@ie = external thread_local(initialexec) global i64
@gd = external thread_local global i64
@ld = internal thread_local(localdynamic) global i64 3, align 32
; This test isolates TLS linking from PIC unwind metadata support.
define i64 @access() nounwind {
  %a = load i64, ptr @ie
  %b = load i64, ptr @gd
  %c = load i64, ptr @ld
  %d = add i64 %a, %b
  %e = add i64 %c, %d
  ret i64 %e
}
