; RUN: llc -mtriple=mmix -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s

; CHECK-LABEL: strict_add_f32:
; CHECK: FMUL
; CHECK: FMUL
; CHECK: FADD
; CHECK: STSF

define float @strict_add_f32(float %lhs, float %rhs) strictfp {
  %result = call float @llvm.experimental.constrained.fadd.f32(
      float %lhs, float %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  ret float %result
}

declare float @llvm.experimental.constrained.fadd.f32(
    float, float, metadata, metadata)
