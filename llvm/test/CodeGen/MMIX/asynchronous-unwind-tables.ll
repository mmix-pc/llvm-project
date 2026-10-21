; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -filetype=obj %s -o %t.o
; RUN: llvm-dwarfdump --eh-frame %t.o | FileCheck %s --check-prefix=DWARF

; Bare uwtable means async. Preserve explicit async and sync spellings too.
; CHECK-LABEL: runtime_unwind:
; CHECK: .cfi_startproc
; CHECK: .cfi_def_cfa r254, 0
; CHECK: .cfi_same_value rJ
; CHECK: POP 0, 0
; CHECK: .cfi_endproc
; DWARF: Return address column: 35
; DWARF: DW_CFA_def_cfa: R254 +0
; DWARF: DW_CFA_same_value: RJ

define void @runtime_unwind() uwtable {
entry:
  ret void
}

declare void @sink(ptr)

; CHECK-LABEL: aligned_dynamic:
; CHECK: .cfi_offset rJ,
; CHECK: .cfi_def_cfa r253, 0
; CHECK: PUSHGO
; CHECK: .cfi_register rJ, r30
; CHECK: SUBU r254, r253, [[SIZE:[0-9]+]]
; CHECK-NEXT: .cfi_def_cfa r254, [[SIZE]]
; CHECK: LDOU r253,
; CHECK-NEXT: .cfi_same_value r253
; CHECK-NEXT: ADDU r254, r254, [[SIZE]]
; CHECK-NEXT: .cfi_def_cfa_offset 0
define void @aligned_dynamic(i64 %n) uwtable(async) {
  %p = alloca i64, i64 %n, align 64
  call void @sink(ptr %p)
  ret void
}

; CHECK-LABEL: synchronous:
; CHECK: .cfi_startproc
; CHECK: .cfi_same_value rJ
define void @synchronous() uwtable(sync) {
  ret void
}
