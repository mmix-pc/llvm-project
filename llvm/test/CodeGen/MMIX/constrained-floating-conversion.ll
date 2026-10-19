; RUN: llc -mtriple=mmix -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s

; CHECK-LABEL: strict_signed_to_double:
; CHECK: FLOT r{{[0-9]+}}, 0,

define double @strict_signed_to_double(i64 %value) strictfp {
  %result = call double @llvm.experimental.constrained.sitofp.f64.i64(
      i64 %value, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %result
}

declare double @llvm.experimental.constrained.sitofp.f64.i64(
    i64, metadata, metadata)

; CHECK-LABEL: strict_unsigned_to_float:
; CHECK: SFLOTU r{{[0-9]+}}, 0,
; CHECK: STSF
define float @strict_unsigned_to_float(i64 %x) strictfp {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i64(i64 %x,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %r
}
declare float @llvm.experimental.constrained.uitofp.f32.i64(i64, metadata, metadata)

; CHECK-LABEL: strict_truncate:
; CHECK: FIXU r{{[0-9]+}}, 1,
define i64 @strict_truncate(double %x) strictfp {
  %r = call i64 @llvm.experimental.constrained.fptosi.i64.f64(double %x,
      metadata !"fpexcept.strict")
  ret i64 %r
}
declare i64 @llvm.experimental.constrained.fptosi.i64.f64(double, metadata)

; CHECK-LABEL: strict_narrow:
; CHECK: STSF
; CHECK: LDTU
define float @strict_narrow(double %x) strictfp {
  %r = call float @llvm.experimental.constrained.fptrunc.f32.f64(double %x,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret float %r
}
declare float @llvm.experimental.constrained.fptrunc.f32.f64(double, metadata, metadata)

; CHECK-LABEL: strict_extend:
; CHECK-NOT: LDSF
; CHECK: FMUL
define double @strict_extend(float %x) strictfp {
  %r = call double @llvm.experimental.constrained.fpext.f64.f32(float %x,
      metadata !"fpexcept.strict")
  ret double %r
}
declare double @llvm.experimental.constrained.fpext.f64.f32(float, metadata)
