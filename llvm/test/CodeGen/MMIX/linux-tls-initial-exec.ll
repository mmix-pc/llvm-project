; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llvm-mc -triple=mmix-unknown-linux -filetype=obj %t.s -o %t.asm.o
; RUN: llvm-readobj -r %t.asm.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %s -o %t.o0.o
; RUN: llvm-readobj -r %t.o0.o | FileCheck %s --check-prefix=REL
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs -filetype=obj %s -o %t.o2.o
; RUN: llvm-readobj -r %t.o2.o | FileCheck %s --check-prefix=REL

@external = external thread_local(initialexec) global [32 x i8], align 32
@defined = thread_local(initialexec) global i64 7, align 8
declare ptr @llvm.thread.pointer()

; CHECK-LABEL: address:
; CHECK: .mmix_gottprel_pc64 external
; CHECK: LDOU
; CHECK: LDOU
; CHECK: OR [[TP:r[0-9]+]], r230, 0
; CHECK: ADDU r231, [[TP]],
; CHECK-NOT: PUSHGO
; CHECK: POP
define ptr @address() { ret ptr @external }

; Source offsets adjust the resolved object, not the GOT slot or its literal.
; CHECK-LABEL: offset:
; CHECK: .mmix_gottprel_pc64 external
; CHECK: OR [[OFFSET_TP:r[0-9]+]], r230, 0
; CHECK: ADDU {{r[0-9]+}}, [[OFFSET_TP]],
; CHECK: {{(ADDU|SUBU)}}
; CHECK: POP
define ptr @offset() { ret ptr getelementptr(i8, ptr @external, i64 -9) }

; CHECK-LABEL: load:
; CHECK: .mmix_gottprel_pc64 defined
; CHECK: LDOU
; CHECK: POP
define i64 @load() { %v = load i64, ptr @defined, align 8 ret i64 %v }

; CHECK-LABEL: store:
; CHECK: .mmix_gottprel_pc64 defined
; CHECK: STOU
; CHECK: POP
define void @store(i64 %v) { store i64 %v, ptr @defined, align 8 ret void }

; Reading TP itself is valid in PIC code and does not invoke a resolver.
; CHECK-LABEL: tp:
; CHECK: r230
; CHECK-NOT: PUSHGO
; CHECK: POP
define ptr @tp() { %p = call ptr @llvm.thread.pointer() ret ptr %p }

; REL-NOT: R_MMIX_GOT_PC64 __tls_get_addr
; REL: R_MMIX_GOTTPREL_PC64 external 0x0
; REL: R_MMIX_GOTTPREL_PC64 external 0x0
; REL: R_MMIX_GOTTPREL_PC64 defined 0x0
; REL: R_MMIX_GOTTPREL_PC64 defined 0x0
; REL-NOT: R_MMIX_GOT_PC64 __tls_get_addr
