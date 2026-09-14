; RUN: llc -mtriple=mmix-unknown-linux -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=LINUX
; RUN: llc -mtriple=mmix-unknown-unknown -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=GENERIC --implicit-check-not=csr_mmix_linux
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM --implicit-check-not=r230 --implicit-check-not='PUT rG'
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ASM --implicit-check-not=r230 --implicit-check-not='PUT rG'
; LINUX: csr_mmix_linux
; LINUX: csr_mmix_linux
; GENERIC: csr_mmix
; GENERIC: csr_mmix
; ASM-LABEL: ordinary:
; ASM: PUSH{{J|GO}}
; ASM-LABEL: fast:
; ASM: PUSH{{J|GO}}
declare i64 @callee(i64)
declare fastcc i64 @fast_callee(i64)
define i64 @ordinary(i64 %x) {
  %a = call i64 @callee(i64 %x)
  %b = add i64 %a, %x
  ret i64 %b
}
define fastcc i64 @fast(i64 %x) {
  %a = call fastcc i64 @fast_callee(i64 %x)
  %b = add i64 %a, %x
  ret i64 %b
}
