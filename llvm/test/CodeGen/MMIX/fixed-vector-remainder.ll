; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs %s -o /dev/null

; Pre-legalization DAG combines query vector comparison types even on a
; scalar-only target. Wider internal vectors do not require a vector call ABI.
; CHECK-LABEL: unsigned_remainder:
; CHECK-COUNT-2: DIVU
; CHECK: POP
define void @unsigned_remainder(ptr %a, ptr %b, ptr %out) {
  %x = load <2 x i64>, ptr %a, align 8
  %y = load <2 x i64>, ptr %b, align 8
  %r = urem <2 x i64> %x, %y
  store <2 x i64> %r, ptr %out, align 8
  ret void
}

; CHECK-LABEL: signed_remainder:
; CHECK-COUNT-2: DIV
; CHECK: POP
define void @signed_remainder(ptr %a, ptr %b, ptr %out) {
  %x = load <2 x i32>, ptr %a, align 4
  %y = load <2 x i32>, ptr %b, align 4
  %r = srem <2 x i32> %x, %y
  store <2 x i32> %r, ptr %out, align 4
  ret void
}

; UREM by all-ones folds to per-lane compare/select before scalarization.
; CHECK-LABEL: remainder_all_ones:
; CHECK-NOT: DIV
; CHECK: CMP
; CHECK-NOT: DIV
; CHECK: POP
define void @remainder_all_ones(ptr %a, ptr %out) {
  %x = load <2 x i64>, ptr %a, align 8
  %r = urem <2 x i64> %x, <i64 -1, i64 -1>
  store <2 x i64> %r, ptr %out, align 8
  ret void
}
