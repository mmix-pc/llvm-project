; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs -filetype=obj %s -o %t.o

; The first memory octa and the first ABI register hold the high half.
; CHECK-LABEL: store_wide:
; CHECK-DAG: STOU r232, r231, 0
; CHECK-DAG: STOU r233, r231, 8
define void @store_wide(ptr %p, i128 %v) {
  store volatile i128 %v, ptr %p, align 8
  ret void
}

; CHECK-LABEL: load_low:
; CHECK: LDOU r231, r231, 8
define i64 @load_low(ptr %p) {
  %v = load i128, ptr %p, align 8
  %lo = trunc i128 %v to i64
  ret i64 %lo
}

; CHECK-LABEL: load_high:
; CHECK: LDOU r231, r231, 0
define i64 @load_high(ptr %p) {
  %v = load i128, ptr %p, align 8
  %hi = lshr i128 %v, 64
  %r = trunc i128 %hi to i64
  ret i64 %r
}

; CHECK-LABEL: zero_extend:
; CHECK: SETL r231, 0
define i128 @zero_extend(i64 %v) {
  %r = zext i64 %v to i128
  ret i128 %r
}

; CHECK-LABEL: sign_extend:
; CHECK: SR {{.*}}, 63
define i128 @sign_extend(i64 %v) {
  %r = sext i64 %v to i128
  ret i128 %r
}

; CHECK-LABEL: packed_copy:
; CHECK-NOT: LDOU
; CHECK-NOT: STOU
; CHECK: POP
define void @packed_copy(ptr %dst, ptr %src) {
  %v = load volatile i128, ptr %src, align 1
  store volatile i128 %v, ptr %dst, align 1
  ret void
}

; CHECK-LABEL: constant_words:
; CHECK: SETL r231, 1
; CHECK: SETL r232, 2
define i128 @constant_words() {
  ret i128 18446744073709551618
}

; CHECK-LABEL: negative_words:
; CHECK: POP
define i128 @negative_words() {
  ret i128 -18446744073709551615
}

; Local aggregate storage retains the same memory representation.
declare void @consume(ptr)
; CHECK-LABEL: local_record:
; CHECK: PUSH{{J|GO}}
define void @local_record(i128 %v) {
  %p = alloca { i64, i128 }, align 8
  %field = getelementptr { i64, i128 }, ptr %p, i64 0, i32 1
  store i128 %v, ptr %field, align 8
  call void @consume(ptr %p)
  ret void
}

; CHECK-LABEL: negate:
; CHECK: NEGU r231,
; CHECK: NEGU r232, 0, r232
; CHECK: POP
define i128 @negate(i128 %v) {
 %r = sub i128 0, %v
 ret i128 %r
}

; CHECK-LABEL: select_wide:
; CHECK: CS
; CHECK: CS
define i128 @select_wide(i1 %c, i128 %a, i128 %b) {
 %r = select i1 %c, i128 %a, i128 %b
 ret i128 %r
}

define i64 @bitcast_low(i128 %v) {
 %pair = bitcast i128 %v to <2 x i64>
 %r = extractelement <2 x i64> %pair, i32 1
 ret i64 %r
}

@words = global i128 18446744073709551618, align 8
; CHECK-LABEL: words:
; CHECK-NEXT: .8byte 1
; CHECK-NEXT: .8byte 2
