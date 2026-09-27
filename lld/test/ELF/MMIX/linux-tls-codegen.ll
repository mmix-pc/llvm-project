; REQUIRES: mmix
; RUN: split-file %s %t
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t/provider.s -o %t/provider.o
; RUN: llc -mtriple=mmix-unknown-linux -O0 -filetype=obj %t/consumer.ll -o %t/o0.o
; RUN: llc -mtriple=mmix-unknown-linux -O2 -filetype=obj %t/consumer.ll -o %t/o2.o
; RUN: ld.lld -m elf64mmix_linux %t/o0.o %t/provider.o -o %t/o0
; RUN: ld.lld -m elf64mmix_linux %t/o2.o %t/provider.o -o %t/o2
; RUN: llvm-objdump -d %t/o0 | FileCheck %s --check-prefix=CODE
; RUN: llvm-objdump -d %t/o2 | FileCheck %s --check-prefix=CODE
; RUN: llvm-readobj -r --program-headers %t/o2 | FileCheck %s --check-prefix=ELF

; The provider's 64-byte alignment requires D=64. No TCB bytes belong in
; PT_TLS; tls has offset zero, so all upper displacement wydes are zero.
; CODE-LABEL: <_start>:
; CODE: SETL [[R:r[0-9]+]], 0x40
; CODE-NEXT: {{.*}}INCML [[R]], 0x0
; CODE-NEXT: {{.*}}INCMH [[R]], 0x0
; CODE-NEXT: {{.*}}INCH [[R]], 0x0
; CODE-NEXT: {{.*}}ADDU [[R]], r230, [[R]]
; CODE: LDOU
; ELF: Type: PT_TLS
; ELF: FileSize: 8
; ELF: MemSize: 8
; ELF: Alignment: 64
; ELF: Relocations [
; ELF-NEXT: ]

;--- consumer.ll
@tls = external thread_local(localexec) global i64
define i64 @_start() {
  %v = load i64, ptr @tls
  ret i64 %v
}

;--- provider.s
.section .tdata,"awT",@progbits
.p2align 6
.globl tls
.type tls,@tls_object
tls: .8byte 42
.size tls, 8
