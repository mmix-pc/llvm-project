; RUN: opt -S -passes=instsimplify %s | FileCheck %s
; RUN: opt -S -passes='default<O2>' %s | FileCheck %s

; Exact cancellation still depends on the dynamic rounding mode.
define double @subtract() strictfp {
; CHECK-LABEL: @subtract(
; CHECK: [[SUB:%.*]] = {{(tail )?}}call double @llvm.experimental.constrained.fsub.f64
; CHECK: ret double [[SUB]]
  %r = call double @llvm.experimental.constrained.fsub.f64(double 1.0, double 1.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @add() strictfp {
; CHECK-LABEL: @add(
; CHECK: [[ADD:%.*]] = {{(tail )?}}call double @llvm.experimental.constrained.fadd.f64
; CHECK: ret double [[ADD]]
  %r = call double @llvm.experimental.constrained.fadd.f64(double 1.0, double -1.0, metadata !"round.dynamic", metadata !"fpexcept.ignore")
  ret double %r
}

define double @fma() strictfp {
; CHECK-LABEL: @fma(
; CHECK: [[FMA:%.*]] = {{(tail )?}}call double @llvm.experimental.constrained.fma.f64
; CHECK: ret double [[FMA]]
  %r = call double @llvm.experimental.constrained.fma.f64(double 2.0, double 3.0, double -6.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @fmuladd() strictfp {
; CHECK-LABEL: @fmuladd(
; CHECK: [[FMULADD:%.*]] = {{(tail )?}}call double @llvm.experimental.constrained.fmuladd.f64
; CHECK: ret double [[FMULADD]]
  %r = call double @llvm.experimental.constrained.fmuladd.f64(double 2.0, double 3.0, double -6.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @opposite_zeros() strictfp {
; CHECK-LABEL: @opposite_zeros(
; CHECK: [[ZERO:%.*]] = {{(tail )?}}call double @llvm.experimental.constrained.fadd.f64
; CHECK: ret double [[ZERO]]
  %r = call double @llvm.experimental.constrained.fadd.f64(double 0.0, double -0.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

; Keep folds whose sign is independent of rounding.
define double @same_zeros() strictfp {
; CHECK-LABEL: @same_zeros(
; CHECK: ret double -0.000000e+00
  %r = call double @llvm.experimental.constrained.fadd.f64(double -0.0, double -0.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @multiply_zero() strictfp {
; CHECK-LABEL: @multiply_zero(
; CHECK: ret double -0.000000e+00
  %r = call double @llvm.experimental.constrained.fmul.f64(double -0.0, double 2.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @exact_nonzero() strictfp {
; CHECK-LABEL: @exact_nonzero(
; CHECK: ret double 3.000000e+00
  %r = call double @llvm.experimental.constrained.fadd.f64(double 1.0, double 2.0, metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}

define double @known_rounding() strictfp {
; CHECK-LABEL: @known_rounding(
; CHECK: ret double -0.000000e+00
  %r = call double @llvm.experimental.constrained.fsub.f64(double 1.0, double 1.0, metadata !"round.downward", metadata !"fpexcept.strict")
  ret double %r
}

declare double @llvm.experimental.constrained.fsub.f64(double, double, metadata, metadata)
declare double @llvm.experimental.constrained.fadd.f64(double, double, metadata, metadata)
declare double @llvm.experimental.constrained.fmul.f64(double, double, metadata, metadata)
declare double @llvm.experimental.constrained.fma.f64(double, double, double, metadata, metadata)
declare double @llvm.experimental.constrained.fmuladd.f64(double, double, double, metadata, metadata)
