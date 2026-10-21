; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mmix -O2 -verify-machineinstrs < %s | FileCheck %s

declare void @observe(ptr, ptr) nounwind

; CHECK-LABEL: aligned_16:
; CHECK: ANDN r254, r254, 15
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
; CHECK: POP
define i64 @aligned_16(i64 %x) nounwind {
  %p = alloca [16 x i8], align 16
  %q = alloca i64, align 8
  store volatile i64 %x, ptr %q
  call void @observe(ptr %p, ptr %q)
  %r = load volatile i64, ptr %q
  ret i64 %r
}

; CHECK-LABEL: incoming_and_spills:
; CHECK: ANDN r254, r254, 63
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
define i64 @incoming_and_spills(i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4, i64 %a5, i64 %a6, i64 %a7, i64 %a8, i64 %a9, i64 %a10, i64 %a11, i64 %a12, i64 %a13, i64 %a14, i64 %a15, i64 %a16, i64 %a17, i64 %a18, i64 %a19, i64 %a20, i64 %a21, i64 %a22, i64 %a23, i64 %a24, i64 %a25, i64 %a26, i64 %a27, i64 %a28, i64 %a29, i64 %a30, i64 %a31, i64 %a32, i64 %a33, i64 %a34, i64 %a35, i64 %a36, i64 %a37, i64 %a38, i64 %a39) nounwind {
  %p = alloca [64 x i8], align 64
  %q = alloca i64, align 8
  store volatile i64 123, ptr %q
  call void @observe(ptr %p, ptr %q)
  %v = load volatile i64, ptr %q
  %s0 = add i64 %v, %a0
  %s1 = add i64 %s0, %a1
  %s2 = add i64 %s1, %a2
  %s3 = add i64 %s2, %a3
  %s4 = add i64 %s3, %a4
  %s5 = add i64 %s4, %a5
  %s6 = add i64 %s5, %a6
  %s7 = add i64 %s6, %a7
  %s8 = add i64 %s7, %a8
  %s9 = add i64 %s8, %a9
  %s10 = add i64 %s9, %a10
  %s11 = add i64 %s10, %a11
  %s12 = add i64 %s11, %a12
  %s13 = add i64 %s12, %a13
  %s14 = add i64 %s13, %a14
  %s15 = add i64 %s14, %a15
  %s16 = add i64 %s15, %a16
  %s17 = add i64 %s16, %a17
  %s18 = add i64 %s17, %a18
  %s19 = add i64 %s18, %a19
  %s20 = add i64 %s19, %a20
  %s21 = add i64 %s20, %a21
  %s22 = add i64 %s21, %a22
  %s23 = add i64 %s22, %a23
  %s24 = add i64 %s23, %a24
  %s25 = add i64 %s24, %a25
  %s26 = add i64 %s25, %a26
  %s27 = add i64 %s26, %a27
  %s28 = add i64 %s27, %a28
  %s29 = add i64 %s28, %a29
  %s30 = add i64 %s29, %a30
  %s31 = add i64 %s30, %a31
  %s32 = add i64 %s31, %a32
  %s33 = add i64 %s32, %a33
  %s34 = add i64 %s33, %a34
  %s35 = add i64 %s34, %a35
  %s36 = add i64 %s35, %a36
  %s37 = add i64 %s36, %a37
  %s38 = add i64 %s37, %a38
  %s39 = add i64 %s38, %a39
  ret i64 %s39
}


; CHECK-LABEL: aligned_32:
; CHECK: ANDN r254, r254, 31
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
; CHECK: POP
define i64 @aligned_32(i64 %x) nounwind {
  %p = alloca [32 x i8], align 32
  %q = alloca i64, align 8
  store volatile i64 %x, ptr %q
  call void @observe(ptr %p, ptr %q)
  %r = load volatile i64, ptr %q
  ret i64 %r
}

; CHECK-LABEL: aligned_64:
; CHECK: ANDN r254, r254, 63
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
; CHECK: POP
define i64 @aligned_64(i64 %x) nounwind {
  %p = alloca [64 x i8], align 64
  %q = alloca i64, align 8
  store volatile i64 %x, ptr %q
  call void @observe(ptr %p, ptr %q)
  %r = load volatile i64, ptr %q
  ret i64 %r
}

; CHECK-LABEL: aligned_4096:
; CHECK: ANDN r254, r254, r255
; CHECK: PUSHGO
; CHECK: SUBU r254, r253,
; CHECK: POP
define i64 @aligned_4096(i64 %x) nounwind {
  %p = alloca [4096 x i8], align 4096
  %q = alloca i64, align 8
  store volatile i64 %x, ptr %q
  call void @observe(ptr %p, ptr %q)
  %r = load volatile i64, ptr %q
  ret i64 %r
}
