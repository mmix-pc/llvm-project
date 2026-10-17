; RUN: llc -mtriple=mmix -verify-machineinstrs -filetype=asm %s -o - | FileCheck %s

target triple = "mmix"

; Binary32 call values remain raw low-tetra encodings. Each operation stores
; its inputs with STTU, extends them exactly with LDSF, performs the binary64
; operation, and rounds the result once with STSF before returning its low
; tetra with LDTU.

; CHECK-LABEL: add_f32:
; CHECK-DAG:   STTU r231, r254,
; CHECK-DAG:   STTU r232, r254,
; CHECK-COUNT-2: LDSF
; CHECK:       FADD [[RESULT:r[0-9]+]], {{r[0-9]+}}, {{r[0-9]+}}
; CHECK:       STSF [[RESULT]], r254,
; CHECK:       LDTU r231, r254,
define float @add_f32(float %lhs, float %rhs) {
  %result = fadd float %lhs, %rhs
  ret float %result
}

; CHECK-LABEL: subtract_f32:
; CHECK:       FSUB
; CHECK:       STSF
; CHECK:       LDTU r231
define float @subtract_f32(float %lhs, float %rhs) {
  %result = fsub float %lhs, %rhs
  ret float %result
}

; CHECK-LABEL: multiply_f32:
; CHECK:       FMUL
; CHECK:       STSF
; CHECK:       LDTU r231
define float @multiply_f32(float %lhs, float %rhs) {
  %result = fmul float %lhs, %rhs
  ret float %result
}

; CHECK-LABEL: divide_f32:
; CHECK:       FDIV
; CHECK:       STSF
; CHECK:       LDTU r231
define float @divide_f32(float %lhs, float %rhs) {
  %result = fdiv float %lhs, %rhs
  ret float %result
}

; CHECK-LABEL: square_root_f32:
; CHECK:       FSQRT {{r[0-9]+}}, 4, {{r[0-9]+}}
; CHECK:       STSF
; CHECK:       LDTU r231
define float @square_root_f32(float %value) {
  %result = call float @llvm.sqrt.f32(float %value)
  ret float %result
}

; Quiet equality implements the required equality of positive and negative
; zero after exact extension.
; CHECK-LABEL: equal_f32:
; CHECK:       FEQL r231
define i1 @equal_f32(float %lhs, float %rhs) {
  %result = fcmp oeq float %lhs, %rhs
  ret i1 %result
}

; Ordered inclusive comparison retains the explicit unordered guard for NaNs.
; CHECK-LABEL: ordered_greater_equal_f32:
; CHECK:       FUN [[UN:r[0-9]+]], [[LHS:r[0-9]+]], [[RHS:r[0-9]+]]
; CHECK-DAG:   CSNZ [[LHS]], [[UN]], 0
; CHECK-DAG:   CSNZ [[RHS]], [[UN]], 0
; CHECK:       FCMP {{r[0-9]+}}, [[LHS]], [[RHS]]
define i1 @ordered_greater_equal_f32(float %lhs, float %rhs) {
  %result = fcmp oge float %lhs, %rhs
  ret i1 %result
}

; Binary32 comparisons must expand before branching, just like their promoted
; binary64 SETCC representation.
; CHECK-LABEL: branch_unordered_f32:
; CHECK:       FUN [[UN:r[0-9]+]], {{r[0-9]+}}, {{r[0-9]+}}
; CHECK:       CMPU [[UN]], [[UN]], 0
; CHECK:       {{P?B}}NZ [[UN]],
define i64 @branch_unordered_f32(float %lhs, float %rhs) {
  %condition = fcmp uno float %lhs, %rhs
  br i1 %condition, label %true, label %false

true:
  ret i64 1

false:
  ret i64 0
}

; Ordinary finite constants are extended exactly before the operation.
; CHECK-LABEL: add_finite_f32:
; CHECK:       FADD
; CHECK:       STSF
define float @add_finite_f32(float %value) {
  %result = fadd float %value, 1.5
  ret float %result
}

; CHECK-LABEL: multiply_negative_zero_f32:
; CHECK:       FMUL
; CHECK:       STSF
define float @multiply_negative_zero_f32(float %value) {
  %result = fmul float %value, -0.0
  ret float %result
}

; CHECK-LABEL: add_infinity_f32:
; CHECK:       FADD
; CHECK:       STSF
define float @add_infinity_f32(float %value) {
  %result = fadd float %value, 0x7FF0000000000000
  ret float %result
}

; CHECK-LABEL: add_nan_f32:
; CHECK:       FADD
; CHECK:       STSF
define float @add_nan_f32(float %value) {
  %result = fadd float %value, 0x7FF8000000000000
  ret float %result
}

declare float @llvm.sqrt.f32(float)
declare float @llvm.fabs.f32(float)
declare float @llvm.copysign.f32(float, float)

; Unary and selection operations promote through binary64 without acquiring
; helper dependencies.
; CHECK-LABEL: absolute_f32:
; CHECK-NOT:   __
define float @absolute_f32(float %value) {
  %result = call float @llvm.fabs.f32(float %value)
  ret float %result
}

; CHECK-LABEL: copy_sign_f32:
; CHECK-NOT:   __
define float @copy_sign_f32(float %magnitude, float %sign) {
  %result = call float @llvm.copysign.f32(float %magnitude, float %sign)
  ret float %result
}

; CHECK-LABEL: select_f32:
; CHECK-NOT:   __
define float @select_f32(i1 %condition, float %true, float %false) {
  %result = select i1 %condition, float %true, float %false
  ret float %result
}
