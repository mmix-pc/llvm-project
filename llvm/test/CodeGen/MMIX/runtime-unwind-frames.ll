; RUN: llc -mtriple=mmix -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: llc -O0 -mtriple=mmix -verify-machineinstrs %s -o %t.o0.s
; RUN: FileCheck %s < %t.o0.s
; RUN: llc -mtriple=mmix -filetype=obj %s -o %t.o
; RUN: llvm-dwarfdump --eh-frame %t.o | FileCheck %s --check-prefix=DWARF
; RUN: llc -O0 -mtriple=mmix -filetype=obj %s -o %t.o0.o
; RUN: llvm-dwarfdump --eh-frame %t.o0.o | FileCheck %s --check-prefix=DWARF

declare void @use(ptr)
declare void @callee()

; CHECK-LABEL: caller:
; CHECK: .cfi_same_value rJ
; CHECK: GET r30, rJ
; CHECK: STOU r30,
; CHECK: .cfi_offset rJ,
; CHECK: PUSH{{J|GO}}
; CHECK: .cfi_register rJ, r30
; CHECK: PUT rJ, r30
; CHECK: .cfi_same_value rJ
define void @caller() uwtable(sync) {
  call void @callee()
  ret void
}

; CHECK-LABEL: fixed_fp:
; CHECK: STOU r253,
; CHECK: .cfi_offset r253,
; CHECK: .cfi_def_cfa r253, 0
; CHECK: PUSH{{J|GO}}
; CHECK: .cfi_def_cfa r254,
; CHECK: LDOU r253,
; CHECK: .cfi_same_value r253
define void @fixed_fp() uwtable(sync) "frame-pointer"="all" {
  %p = alloca [320 x i8], align 8
  call void @use(ptr %p)
  ret void
}

; CHECK-LABEL: dynamic:
; CHECK: .cfi_def_cfa r253, 0
; CHECK: PUSH{{J|GO}}
; CHECK: SUBU r254, r253, [[FRAME:[0-9]+]]
; CHECK: .cfi_def_cfa r254, [[FRAME]]
; CHECK: LDOU r253,
; CHECK: .cfi_same_value r253
; CHECK: ADDU r254, r254, [[FRAME]]
; CHECK: .cfi_def_cfa_offset 0
define void @dynamic(i64 %n) uwtable(sync) {
  %p = alloca i8, i64 %n, align 8
  call void @use(ptr %p)
  ret void
}

; CHECK-LABEL: leaf_frame:
; CHECK: SUBU r254, r254,
; CHECK: .cfi_def_cfa_offset {{[1-9][0-9]*}}
; CHECK: STOU
; CHECK-NOT: .cfi_offset rJ
; CHECK: ADDU r254, r254,
; CHECK: .cfi_def_cfa_offset 0
; CHECK: POP 0, 0
define void @leaf_frame(i64 %value) uwtable(sync) {
  %p = alloca i64, align 8
  store volatile i64 %value, ptr %p, align 8
  ret void
}

; DWARF: Return address column: 35
; DWARF: DW_CFA_offset: RJ -
; DWARF: DW_CFA_register: RJ R30
; DWARF: DW_CFA_same_value: RJ
; DWARF: DW_CFA_def_cfa: R253 +0
