; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix -O2 -verify-machineinstrs %s -o - | FileCheck %s

; CHECK-LABEL: wide_divide:
; CHECK: __udivti3
; CHECK: PUSH{{J|GO}}

define i64 @wide_divide(i64 %lhs, i64 %rhs) {
  %lhs.wide = zext i64 %lhs to i128
  %rhs.wide = zext i64 %rhs to i128
  %quotient = udiv i128 %lhs.wide, %rhs.wide
  %result = trunc i128 %quotient to i64
  ret i64 %result
}
