; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix -O2 -verify-machineinstrs %s -o - | FileCheck %s

; A signal can arrive between any two instructions, including the old-FP
; reload and the final SP adjustment. The save area must remain allocated
; until CFI no longer references it.
declare void @consume(ptr)

; CHECK-LABEL: dynamic_save_area:
; CHECK: .cfi_def_cfa r253, 0
; CHECK: PUSHGO
; CHECK: .cfi_register rJ, r30
; CHECK: SUBU r254, r253, [[SIZE:[0-9]+]]
; CHECK-NEXT: .cfi_def_cfa r254, [[SIZE]]
; CHECK: LDOU r253,
; CHECK-NEXT: .cfi_same_value r253
; CHECK-NEXT: ADDU r254, r254, [[SIZE]]
; CHECK-NEXT: .cfi_def_cfa_offset 0
; CHECK-NEXT: PUT rJ, r30
; CHECK-NEXT: .cfi_same_value rJ
; CHECK-NEXT: POP 0, 0
define void @dynamic_save_area(i64 %size) uwtable(sync) {
  %p = alloca i8, i64 %size, align 8
  call void @consume(ptr %p)
  ret void
}

; CHECK-LABEL: realigned_save_area:
; CHECK: .cfi_def_cfa r253, 0
; CHECK: ANDN r254, r254, 63
; CHECK: PUSHGO
; CHECK: .cfi_register rJ, r30
; CHECK: SUBU r254, r253, [[SIZE:[0-9]+]]
; CHECK-NEXT: .cfi_def_cfa r254, [[SIZE]]
; CHECK: LDOU r253,
; CHECK-NEXT: .cfi_same_value r253
; CHECK-NEXT: ADDU r254, r254, [[SIZE]]
; CHECK-NEXT: .cfi_def_cfa_offset 0
define void @realigned_save_area() uwtable(sync) {
  %p = alloca i64, align 64
  call void @consume(ptr %p)
  ret void
}
