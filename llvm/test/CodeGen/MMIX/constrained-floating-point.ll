; RUN: llc -mtriple=mmix -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s

; CHECK-LABEL: strict_add:
; CHECK: FADD

define double @strict_add(double %lhs, double %rhs) strictfp {
  %result = call double @llvm.experimental.constrained.fadd.f64(
      double %lhs, double %rhs, metadata !"round.dynamic",
      metadata !"fpexcept.strict")
  ret double %result
}

declare double @llvm.experimental.constrained.fadd.f64(
    double, double, metadata, metadata)

; CHECK-LABEL: strict_subtract:
; CHECK: FSUB
define double @strict_subtract(double %x, double %y) strictfp {
  %r = call double @llvm.experimental.constrained.fsub.f64(double %x, double %y,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}
declare double @llvm.experimental.constrained.fsub.f64(double, double, metadata, metadata)

; CHECK-LABEL: strict_multiply:
; CHECK: FMUL
define double @strict_multiply(double %x, double %y) strictfp {
  %r = call double @llvm.experimental.constrained.fmul.f64(double %x, double %y,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}
declare double @llvm.experimental.constrained.fmul.f64(double, double, metadata, metadata)

; CHECK-LABEL: strict_sqrt:
; CHECK: FSQRT r{{[0-9]+}}, 0,
define double @strict_sqrt(double %x) strictfp {
  %r = call double @llvm.experimental.constrained.sqrt.f64(double %x,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}
declare double @llvm.experimental.constrained.sqrt.f64(double, metadata, metadata)

; CHECK-LABEL: strict_rint:
; CHECK: FINT r{{[0-9]+}}, 0,
define double @strict_rint(double %x) strictfp {
  %r = call double @llvm.experimental.constrained.rint.f64(double %x,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}
declare double @llvm.experimental.constrained.rint.f64(double, metadata, metadata)

; CHECK-LABEL: strict_discarded:
; CHECK: FDIV
; CHECK: POP
define void @strict_discarded(double %x, double %y) strictfp {
  %r = call double @llvm.experimental.constrained.fdiv.f64(double %x, double %y,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret void
}
declare double @llvm.experimental.constrained.fdiv.f64(double, double, metadata, metadata)

; CHECK-LABEL: strict_quiet_equal:
; CHECK: FMUL
; CHECK: FMUL
; CHECK: FEQL
define i1 @strict_quiet_equal(double %x, double %y) strictfp {
  %r = call i1 @llvm.experimental.constrained.fcmp.f64(double %x, double %y,
      metadata !"oeq", metadata !"fpexcept.strict")
  ret i1 %r
}
declare i1 @llvm.experimental.constrained.fcmp.f64(double, double, metadata, metadata)

; CHECK-LABEL: strict_signaling_equal:
; CHECK: FCMP
; CHECK: FEQL
define i1 @strict_signaling_equal(double %x, double %y) strictfp {
  %r = call i1 @llvm.experimental.constrained.fcmps.f64(double %x, double %y,
      metadata !"oeq", metadata !"fpexcept.strict")
  ret i1 %r
}
declare i1 @llvm.experimental.constrained.fcmps.f64(double, double, metadata, metadata)

; CHECK-LABEL: strict_order:
; CHECK: FADD
; CHECK: PUSHGO
; CHECK: FADD
define double @strict_order(double %x, double %y) strictfp {
  %a = call double @llvm.experimental.constrained.fadd.f64(double %x, double %y,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  call void @change_environment()
  %b = call double @llvm.experimental.constrained.fadd.f64(double %x, double %y,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  %r = call double @llvm.experimental.constrained.fadd.f64(double %a, double %b,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}
declare void @change_environment()
