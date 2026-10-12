; RUN: llc -mtriple=mmix -verify-machineinstrs -filetype=asm %s -o - | FileCheck %s

target triple = "mmix"

; Branch optimization may reverse the condition to make the true block fall
; through. The resulting direction-neutral predicate becomes forward BNN.
; CHECK-LABEL: branch_signed:
; CHECK:       CMP [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BNN [[CMP]], [[FALSE:.LBB[0-9_]+]]
; CHECK:       [[FALSE]]:
define i64 @branch_signed(i64 %lhs, i64 %rhs) {
entry:
  %condition = icmp slt i64 %lhs, %rhs
  br i1 %condition, label %true, label %false

true:
  ret i64 1

false:
  ret i64 2
}

; Equality against an encodable constant uses compare-immediate. Layout makes
; the equal block fall through, so branch reversal emits BNZ.
; CHECK-LABEL: branch_immediate:
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, 255
; CHECK-NEXT:  BNZ [[CMP]],
define i64 @branch_immediate(i64 %value) {
entry:
  %condition = icmp eq i64 %value, 255
  br i1 %condition, label %equal, label %different

equal:
  ret i64 3

different:
  ret i64 4
}

; The following checks cover the remaining signed and unsigned relations.
; Block layout makes each true block fall through, exercising condition
; reversal as well as the original relation mapping.
; CHECK-LABEL: branch_sle:
; CHECK:       CMP [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BP [[CMP]],
define i64 @branch_sle(i64 %lhs, i64 %rhs) {
  %condition = icmp sle i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_sgt:
; CHECK:       CMP [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BNP [[CMP]],
define i64 @branch_sgt(i64 %lhs, i64 %rhs) {
  %condition = icmp sgt i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_sge:
; CHECK:       CMP [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BN [[CMP]],
define i64 @branch_sge(i64 %lhs, i64 %rhs) {
  %condition = icmp sge i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_ult:
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BNN [[CMP]],
define i64 @branch_ult(i64 %lhs, i64 %rhs) {
  %condition = icmp ult i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_ule:
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BP [[CMP]],
define i64 @branch_ule(i64 %lhs, i64 %rhs) {
  %condition = icmp ule i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_ugt:
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BNP [[CMP]],
define i64 @branch_ugt(i64 %lhs, i64 %rhs) {
  %condition = icmp ugt i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_uge:
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BN [[CMP]],
define i64 @branch_uge(i64 %lhs, i64 %rhs) {
  %condition = icmp uge i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; CHECK-LABEL: branch_ne:
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, r232
; CHECK-NEXT:  BZ [[CMP]],
define i64 @branch_ne(i64 %lhs, i64 %rhs) {
  %condition = icmp ne i64 %lhs, %rhs
  br i1 %condition, label %true, label %false
true:
  ret i64 1
false:
  ret i64 0
}

; A loop backedge selects the backward opcode only after block placement.
; CHECK-LABEL: backward_loop:
; CHECK:       [[LOOP:.LBB[0-9_]+]]:
; CHECK:       CMPU [[CMP:r[0-9]+]],
; CHECK-NEXT:  BNZB [[CMP]], [[LOOP]]
define i64 @backward_loop(i64 %count) {
entry:
  br label %loop

loop:
  %value = phi i64 [ %count, %entry ], [ %next, %loop ]
  %next = add i64 %value, -1
  %condition = icmp ne i64 %next, 0
  br i1 %condition, label %loop, label %exit

exit:
  ret i64 %next
}

; A non-comparison i1 condition branches on nonzero after ABI legalization.
; CHECK-LABEL: branch_boolean:
; CHECK:       AND [[COND:r[0-9]+]], r231, 1
; CHECK-NEXT:  CMPU [[CMP:r[0-9]+]], [[COND]], 0
; CHECK-NEXT:  BZ [[CMP]],
define i64 @branch_boolean(i1 %condition) {
entry:
  br i1 %condition, label %true, label %false

true:
  ret i64 5

false:
  ret i64 6
}

; Branch weights do not select MMIX probable branches until the backend has a
; reviewed probability threshold and late direction-aware hint policy.
; CHECK-LABEL: branch_profile_hint:
; CHECK-NOT:   PB
; CHECK:       CMPU [[CMP:r[0-9]+]], r231, 0
; CHECK-NEXT:  BZ [[CMP]],
define i64 @branch_profile_hint(i64 %value) {
entry:
  %condition = icmp ne i64 %value, 0
  br i1 %condition, label %likely, label %unlikely, !prof !0

likely:
  ret i64 1

unlikely:
  ret i64 0
}

; Automatic switch jump tables remain disabled independently of support for
; explicit blockaddress/indirectbr control flow.
; CHECK-LABEL: switch_chain:
; CHECK-NOT:   GO
; CHECK-COUNT-2: BZ
define i64 @switch_chain(i64 %value) {
entry:
  switch i64 %value, label %default [
    i64 1, label %one
    i64 2, label %two
    i64 3, label %three
    i64 4, label %four
  ]

one:
  ret i64 10
two:
  ret i64 20
three:
  ret i64 30
four:
  ret i64 40
default:
  ret i64 0
}

!0 = !{!"branch_weights", i32 1000, i32 1}
