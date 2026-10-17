; RUN: llc -mtriple=mmix -verify-machineinstrs -filetype=asm %s -o - | FileCheck %s

target triple = "mmix"

; CHECK-LABEL: add:
; CHECK:       FADD r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define double @add(double %lhs, double %rhs) nounwind {
  %result = fadd double %lhs, %rhs
  ret double %result
}

; CHECK-LABEL: subtract:
; CHECK:       FSUB r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define double @subtract(double %lhs, double %rhs) nounwind {
  %result = fsub double %lhs, %rhs
  ret double %result
}

; CHECK-LABEL: multiply:
; CHECK:       FMUL r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define double @multiply(double %lhs, double %rhs) nounwind {
  %result = fmul double %lhs, %rhs
  ret double %result
}

; CHECK-LABEL: divide:
; CHECK:       FDIV r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define double @divide(double %lhs, double %rhs) nounwind {
  %result = fdiv double %lhs, %rhs
  ret double %result
}

; Fast-math flags relax semantics but do not require a different instruction.
; CHECK-LABEL: add_fast:
; CHECK:       FADD r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define double @add_fast(double %lhs, double %rhs) nounwind {
  %result = fadd fast double %lhs, %rhs
  ret double %result
}

; Sign-only operations expand through integer bit operations so NaN payloads
; and all non-sign bits remain unchanged.
; CHECK-LABEL: negate:
; CHECK:       XOR
; CHECK-NOT:   FADD
; CHECK-NOT:   FSUB
define double @negate(double %value) {
  %result = fneg double %value
  ret double %result
}

; CHECK-LABEL: absolute:
; CHECK:       NOR
; CHECK:       AND
; CHECK-NOT:   FADD
define double @absolute(double %value) {
  %result = call double @llvm.fabs.f64(double %value)
  ret double %result
}

; CHECK-LABEL: copy_sign:
; CHECK:       AND
; CHECK:       NOR
; CHECK:       AND
; CHECK:       OR
; CHECK-NOT:   FADD
define double @copy_sign(double %magnitude, double %sign) {
  %result = call double @llvm.copysign.f64(double %magnitude, double %sign)
  ret double %result
}

; Floating select reuses integer bit selection so either complete input,
; including a signaling NaN payload, reaches the result unchanged.
; CHECK-LABEL: select_double:
; CHECK:       CSNZ
; CHECK-NOT:   FCMP
define double @select_double(i1 %condition, double %if_true,
                             double %if_false) {
  %result = select i1 %condition, double %if_true, double %if_false
  ret double %result
}

; CHECK-LABEL: square_root:
; CHECK:       FSQRT r231, 4, r231
; CHECK-NEXT:  POP 0, 0
define double @square_root(double %value) nounwind {
  %result = call double @llvm.sqrt.f64(double %value)
  ret double %result
}

; FEQL implements quiet ordered equality and treats both signed zeros as equal.
; CHECK-LABEL: ordered_equal_signed_zero:
; CHECK-NOT:   FEQLE
; CHECK:       FEQL r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define i1 @ordered_equal_signed_zero(double %lhs, double %rhs) nounwind {
  %result = fcmp oeq double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: ordered_greater:
; CHECK-NOT:   FCMPE
; Quiet comparisons must sanitize both operands before FCMP, not just mask its
; result after an unordered comparison has already raised invalid.
; CHECK:       FUN [[UN:r[0-9]+]], r231, r232
; CHECK-DAG:   CSNZ r231, [[UN]], 0
; CHECK-DAG:   CSNZ r232, [[UN]], 0
; CHECK:       FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       ZSP r231, [[CMP]], 1
define i1 @ordered_greater(double %lhs, double %rhs) {
  %result = fcmp ogt double %lhs, %rhs
  ret i1 %result
}

; FCMP returns zero for unordered operands, so inclusive ordered predicates
; also need FUN to reject NaNs.
; CHECK-LABEL: ordered_greater_equal:
; CHECK-DAG:   FUN [[UN:r[0-9]+]], r231, r232
; CHECK-DAG:   FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       AND
define i1 @ordered_greater_equal(double %lhs, double %rhs) {
  %result = fcmp oge double %lhs, %rhs
  ret i1 %result
}

; With nnan, the unordered outcome is poison and no FUN guard is needed.
; CHECK-LABEL: no_nans_greater_equal:
; CHECK-NOT:   FUN
; CHECK:       FCMP
; CHECK:       POP 0, 0
define i1 @no_nans_greater_equal(double %lhs, double %rhs) {
  %result = fcmp nnan oge double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: ordered_less:
; CHECK:       FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       ZSN r231, [[CMP]], 1
define i1 @ordered_less(double %lhs, double %rhs) {
  %result = fcmp olt double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: ordered_less_equal:
; CHECK-DAG:   FUN [[UN:r[0-9]+]], r231, r232
; CHECK-DAG:   FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       AND
define i1 @ordered_less_equal(double %lhs, double %rhs) {
  %result = fcmp ole double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: ordered_not_equal:
; CHECK:       FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       ZSNZ r231, [[CMP]], 1
define i1 @ordered_not_equal(double %lhs, double %rhs) {
  %result = fcmp one double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: ordered:
; CHECK:       FUN [[UN:r[0-9]+]], r231, r232
; CHECK:       ZSZ r231, [[UN]], 1
define i1 @ordered(double %lhs, double %rhs) {
  %result = fcmp ord double %lhs, %rhs
  ret i1 %result
}

; FUN is a quiet unordered test, including for signaling NaNs.
; CHECK-LABEL: unordered:
; CHECK-NOT:   FUNE
; CHECK:       FUN r231, r231, r232
; CHECK-NEXT:  POP 0, 0
define i1 @unordered(double %lhs, double %rhs) nounwind {
  %result = fcmp uno double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: unordered_or_equal:
; CHECK-DAG:   FEQL [[EQUAL:r[0-9]+]], r231, r232
; CHECK-DAG:   FUN [[UN:r[0-9]+]], r231, r232
; CHECK:       OR r231, [[EQUAL]], [[UN]]
define i1 @unordered_or_equal(double %lhs, double %rhs) {
  %result = fcmp ueq double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: unordered_or_greater:
; CHECK-DAG:   FUN
; CHECK-DAG:   FCMP
; CHECK:       OR
define i1 @unordered_or_greater(double %lhs, double %rhs) {
  %result = fcmp ugt double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: unordered_or_greater_equal:
; CHECK:       FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       NEGU [[NEGONE:r[0-9]+]], 0, 1
; CHECK:       CMP [[CMP]], [[CMP]], [[NEGONE]]
; CHECK:       ZSP r231, [[CMP]], 1
define i1 @unordered_or_greater_equal(double %lhs, double %rhs) {
  %result = fcmp uge double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: unordered_or_less:
; CHECK-DAG:   FUN
; CHECK-DAG:   FCMP
; CHECK:       OR
define i1 @unordered_or_less(double %lhs, double %rhs) {
  %result = fcmp ult double %lhs, %rhs
  ret i1 %result
}

; CHECK-LABEL: unordered_or_less_equal:
; CHECK:       FCMP [[CMP:r[0-9]+]], r231, r232
; CHECK:       CMP [[CMP]], [[CMP]], 1
; CHECK:       ZSN r231, [[CMP]], 1
define i1 @unordered_or_less_equal(double %lhs, double %rhs) {
  %result = fcmp ule double %lhs, %rhs
  ret i1 %result
}

; FEQL returns zero for both unequal and unordered operands.
; CHECK-LABEL: unordered_or_not_equal:
; CHECK:       FEQL [[EQUAL:r[0-9]+]], r231, r232
; CHECK:       ZSZ r231, [[EQUAL]], 1
define i1 @unordered_or_not_equal(double %lhs, double %rhs) {
  %result = fcmp une double %lhs, %rhs
  ret i1 %result
}

; Floating comparisons feed the existing integer Boolean branch path.
; CHECK-LABEL: branch_unordered:
; CHECK:       FUN [[UN:r[0-9]+]], r231, r232
; CHECK:       CMPU [[UN]], [[UN]], 0
; CHECK:       {{P?B}}NZ [[UN]],
define i64 @branch_unordered(double %lhs, double %rhs) {
  %condition = fcmp uno double %lhs, %rhs
  br i1 %condition, label %true, label %false

true:
  ret i64 1

false:
  ret i64 0
}


declare double @llvm.sqrt.f64(double)
declare double @llvm.fabs.f64(double)
declare double @llvm.copysign.f64(double, double)
