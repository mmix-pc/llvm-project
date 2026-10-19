; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mmix %t/from.ll -o /dev/null 2>&1 | FileCheck %s
; RUN: not --crash llc -mtriple=mmix %t/to.ll -o /dev/null 2>&1 | FileCheck %s
; CHECK: MMIX constrained floating-point conversions require integers of at most 64 bits

;--- from.ll
define double @from_wide(i128 %x) strictfp {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i128(i128 %x,
      metadata !"round.dynamic", metadata !"fpexcept.strict")
  ret double %r
}
declare double @llvm.experimental.constrained.sitofp.f64.i128(i128, metadata, metadata)

;--- to.ll
define i128 @to_wide(double %x) strictfp {
  %r = call i128 @llvm.experimental.constrained.fptosi.i128.f64(double %x,
      metadata !"fpexcept.strict")
  ret i128 %r
}
declare i128 @llvm.experimental.constrained.fptosi.i128.f64(double, metadata)
