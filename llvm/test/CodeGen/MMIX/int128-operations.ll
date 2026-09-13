; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o %t.O0.s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o %t.O2.s
; RUN: llc -mtriple=mmix-unknown-unknown -O0 -verify-machineinstrs -filetype=obj %s -o %t.O0.o
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs -filetype=obj %s -o %t.O2.o
; RUN: FileCheck %s < %t.O2.s
; RUN: llvm-nm --undefined-only %t.O0.o | FileCheck %s --check-prefix=HELPER
; RUN: llvm-nm --undefined-only %t.O2.o | FileCheck %s --check-prefix=HELPER

; HELPER-DAG: __udivti3
; HELPER-DAG: __divti3
; HELPER-DAG: __umodti3
; HELPER-DAG: __modti3
; HELPER-DAG: __muloti4
; HELPER-DAG: __floattisf
; HELPER-DAG: __floatuntisf
; HELPER-DAG: __floattidf
; HELPER-DAG: __floatuntidf
; HELPER-DAG: __fixsfti
; HELPER-DAG: __fixunssfti
; HELPER-DAG: __fixdfti
; HELPER-DAG: __fixunsdfti

; High halves add independently; unsigned low-half overflow contributes carry.
; CHECK-LABEL: wide_add:
; CHECK: ADDU [[HI:r[0-9]+]], r231, r233
; CHECK: ADDU [[LO:r[0-9]+]], r232, r234
; CHECK: CMPU [[C:r[0-9]+]], [[LO]], r232
; CHECK: ZSN [[C]], [[C]], 1
; CHECK: ADDU r231, [[HI]], [[C]]
; CHECK: OR r232, [[LO]], 0
define i128 @wide_add(i128 %a, i128 %b) {
  %v = add i128 %a, %b
  ret i128 %v
}

; CHECK-LABEL: wide_sub:
; CHECK: SUBU [[HI:r[0-9]+]], r231, r233
; CHECK: CMPU [[B:r[0-9]+]], r232, r234
; CHECK: ZSN [[B]], [[B]], 1
; CHECK: SUBU r231, [[HI]], [[B]]
; CHECK: SUBU r232, r232, r234
define i128 @wide_sub(i128 %a, i128 %b) {
  %v = sub i128 %a, %b
  ret i128 %v
}

; CHECK-LABEL: wide_mul:
; CHECK: MULU
; CHECK: MULU
; CHECK: GET {{.*}}, rH
; CHECK: ADDU
; CHECK: MULU
; CHECK: ADDU r231,
define i128 @wide_mul(i128 %a, i128 %b) {
  %v = mul i128 %a, %b
  ret i128 %v
}

define i128 @wide_udiv(i128 %a, i128 %b) {
  %v = udiv i128 %a, %b
  ret i128 %v
}

define i128 @wide_sdiv(i128 %a, i128 %b) {
  %v = sdiv i128 %a, %b
  ret i128 %v
}

define i128 @wide_urem(i128 %a, i128 %b) {
  %v = urem i128 %a, %b
  ret i128 %v
}

define i128 @wide_srem(i128 %a, i128 %b) {
  %v = srem i128 %a, %b
  ret i128 %v
}

define i128 @wide_and(i128 %a, i128 %b) {
  %v = and i128 %a, %b
  ret i128 %v
}

define i128 @wide_or(i128 %a, i128 %b) {
  %v = or i128 %a, %b
  ret i128 %v
}

define i128 @wide_xor(i128 %a, i128 %b) {
  %v = xor i128 %a, %b
  ret i128 %v
}

define i128 @wide_shl(i128 %a, i128 %b) {
  %v = shl i128 %a, %b
  ret i128 %v
}

define i128 @wide_lshr(i128 %a, i128 %b) {
  %v = lshr i128 %a, %b
  ret i128 %v
}

define i128 @wide_ashr(i128 %a, i128 %b) {
  %v = ashr i128 %a, %b
  ret i128 %v
}

define i1 @wide_eq(i128 %a, i128 %b) {
  %v = icmp eq i128 %a, %b
  ret i1 %v
}

define i1 @wide_ne(i128 %a, i128 %b) {
  %v = icmp ne i128 %a, %b
  ret i1 %v
}

define i1 @wide_slt(i128 %a, i128 %b) {
  %v = icmp slt i128 %a, %b
  ret i1 %v
}

define i1 @wide_sle(i128 %a, i128 %b) {
  %v = icmp sle i128 %a, %b
  ret i1 %v
}

define i1 @wide_sgt(i128 %a, i128 %b) {
  %v = icmp sgt i128 %a, %b
  ret i1 %v
}

define i1 @wide_sge(i128 %a, i128 %b) {
  %v = icmp sge i128 %a, %b
  ret i1 %v
}

define i1 @wide_ult(i128 %a, i128 %b) {
  %v = icmp ult i128 %a, %b
  ret i1 %v
}

define i1 @wide_ule(i128 %a, i128 %b) {
  %v = icmp ule i128 %a, %b
  ret i1 %v
}

define i1 @wide_ugt(i128 %a, i128 %b) {
  %v = icmp ugt i128 %a, %b
  ret i1 %v
}

define i1 @wide_uge(i128 %a, i128 %b) {
  %v = icmp uge i128 %a, %b
  ret i1 %v
}

define float @wide_sitofp_float(i128 %a) {
  %v = sitofp i128 %a to float
  ret float %v
}

define float @wide_uitofp_float(i128 %a) {
  %v = uitofp i128 %a to float
  ret float %v
}

define double @wide_sitofp_double(i128 %a) {
  %v = sitofp i128 %a to double
  ret double %v
}

define double @wide_uitofp_double(i128 %a) {
  %v = uitofp i128 %a to double
  ret double %v
}

define i128 @wide_fptosi_float(float %a) {
  %v = fptosi float %a to i128
  ret i128 %v
}

define i128 @wide_fptoui_float(float %a) {
  %v = fptoui float %a to i128
  ret i128 %v
}

define i128 @wide_fptosi_double(double %a) {
  %v = fptosi double %a to i128
  ret i128 %v
}

define i128 @wide_fptoui_double(double %a) {
  %v = fptoui double %a to i128
  ret i128 %v
}

define i128 @shl_0(i128 %a) {
 %r = shl i128 %a, 0
 ret i128 %r
}

define i128 @shl_63(i128 %a) {
 %r = shl i128 %a, 63
 ret i128 %r
}

define i128 @shl_64(i128 %a) {
 %r = shl i128 %a, 64
 ret i128 %r
}

define i128 @shl_127(i128 %a) {
 %r = shl i128 %a, 127
 ret i128 %r
}

define i128 @lshr_0(i128 %a) {
 %r = lshr i128 %a, 0
 ret i128 %r
}

define i128 @lshr_63(i128 %a) {
 %r = lshr i128 %a, 63
 ret i128 %r
}

define i128 @lshr_64(i128 %a) {
 %r = lshr i128 %a, 64
 ret i128 %r
}

define i128 @lshr_127(i128 %a) {
 %r = lshr i128 %a, 127
 ret i128 %r
}

define i128 @ashr_0(i128 %a) {
 %r = ashr i128 %a, 0
 ret i128 %r
}

define i128 @ashr_63(i128 %a) {
 %r = ashr i128 %a, 63
 ret i128 %r
}

define i128 @ashr_64(i128 %a) {
 %r = ashr i128 %a, 64
 ret i128 %r
}

define i128 @ashr_127(i128 %a) {
 %r = ashr i128 %a, 127
 ret i128 %r
}

declare { i128, i1 } @llvm.sadd.with.overflow.i128(i128, i128)
define i128 @overflow_sadd(i128 %a, i128 %b, ptr %overflow) {
 %pair = call { i128, i1 } @llvm.sadd.with.overflow.i128(i128 %a, i128 %b)
 %v = extractvalue { i128, i1 } %pair, 0
 %o = extractvalue { i128, i1 } %pair, 1
 store i1 %o, ptr %overflow
 ret i128 %v
}

declare { i128, i1 } @llvm.uadd.with.overflow.i128(i128, i128)
define i128 @overflow_uadd(i128 %a, i128 %b, ptr %overflow) {
 %pair = call { i128, i1 } @llvm.uadd.with.overflow.i128(i128 %a, i128 %b)
 %v = extractvalue { i128, i1 } %pair, 0
 %o = extractvalue { i128, i1 } %pair, 1
 store i1 %o, ptr %overflow
 ret i128 %v
}

declare { i128, i1 } @llvm.ssub.with.overflow.i128(i128, i128)
define i128 @overflow_ssub(i128 %a, i128 %b, ptr %overflow) {
 %pair = call { i128, i1 } @llvm.ssub.with.overflow.i128(i128 %a, i128 %b)
 %v = extractvalue { i128, i1 } %pair, 0
 %o = extractvalue { i128, i1 } %pair, 1
 store i1 %o, ptr %overflow
 ret i128 %v
}

declare { i128, i1 } @llvm.usub.with.overflow.i128(i128, i128)
define i128 @overflow_usub(i128 %a, i128 %b, ptr %overflow) {
 %pair = call { i128, i1 } @llvm.usub.with.overflow.i128(i128 %a, i128 %b)
 %v = extractvalue { i128, i1 } %pair, 0
 %o = extractvalue { i128, i1 } %pair, 1
 store i1 %o, ptr %overflow
 ret i128 %v
}

declare { i128, i1 } @llvm.smul.with.overflow.i128(i128, i128)
define i128 @overflow_smul(i128 %a, i128 %b, ptr %overflow) {
 %pair = call { i128, i1 } @llvm.smul.with.overflow.i128(i128 %a, i128 %b)
 %v = extractvalue { i128, i1 } %pair, 0
 %o = extractvalue { i128, i1 } %pair, 1
 store i1 %o, ptr %overflow
 ret i128 %v
}

declare { i128, i1 } @llvm.umul.with.overflow.i128(i128, i128)
define i128 @overflow_umul(i128 %a, i128 %b, ptr %overflow) {
 %pair = call { i128, i1 } @llvm.umul.with.overflow.i128(i128 %a, i128 %b)
 %v = extractvalue { i128, i1 } %pair, 0
 %o = extractvalue { i128, i1 } %pair, 1
 store i1 %o, ptr %overflow
 ret i128 %v
}

define i128 @udivrem(i128 %a, i128 %b, ptr %rem) {
 %q = udiv i128 %a, %b
 %r = urem i128 %a, %b
 store i128 %r, ptr %rem, align 8
 ret i128 %q
}

define i128 @sdivrem(i128 %a, i128 %b, ptr %rem) {
 %q = sdiv i128 %a, %b
 %r = srem i128 %a, %b
 store i128 %r, ptr %rem, align 8
 ret i128 %q
}

define i128 @mul_three(i128 %a) {
 %r = mul i128 %a, 3
 ret i128 %r
}

define i128 @mul_wide(i128 %a) {
 %r = mul i128 %a, 18446744073709551617
 ret i128 %r
}

define i128 @udiv_three(i128 %a) {
 %r = udiv i128 %a, 3
 ret i128 %r
}

define i128 @udiv_wide(i128 %a) {
 %r = udiv i128 %a, 18446744073709551617
 ret i128 %r
}

define i128 @sdiv_three(i128 %a) {
 %r = sdiv i128 %a, 3
 ret i128 %r
}

define i128 @sdiv_wide(i128 %a) {
 %r = sdiv i128 %a, 18446744073709551617
 ret i128 %r
}

define i128 @urem_three(i128 %a) {
 %r = urem i128 %a, 3
 ret i128 %r
}

define i128 @urem_wide(i128 %a) {
 %r = urem i128 %a, 18446744073709551617
 ret i128 %r
}

define i128 @srem_three(i128 %a) {
 %r = srem i128 %a, 3
 ret i128 %r
}

define i128 @srem_wide(i128 %a) {
 %r = srem i128 %a, 18446744073709551617
 ret i128 %r
}

define float @sitofp_float_zero() {
 %r = sitofp i128 0 to float
 ret float %r
}

define float @sitofp_float_power() {
 %r = sitofp i128 18446744073709551616 to float
 ret float %r
}

define float @sitofp_float_round() {
 %r = sitofp i128 18446744073709553664 to float
 ret float %r
}

define double @sitofp_double_zero() {
 %r = sitofp i128 0 to double
 ret double %r
}

define double @sitofp_double_power() {
 %r = sitofp i128 18446744073709551616 to double
 ret double %r
}

define double @sitofp_double_round() {
 %r = sitofp i128 18446744073709553664 to double
 ret double %r
}

define float @uitofp_float_zero() {
 %r = uitofp i128 0 to float
 ret float %r
}

define float @uitofp_float_power() {
 %r = uitofp i128 18446744073709551616 to float
 ret float %r
}

define float @uitofp_float_round() {
 %r = uitofp i128 18446744073709553664 to float
 ret float %r
}

define double @uitofp_double_zero() {
 %r = uitofp i128 0 to double
 ret double %r
}

define double @uitofp_double_power() {
 %r = uitofp i128 18446744073709551616 to double
 ret double %r
}

define double @uitofp_double_round() {
 %r = uitofp i128 18446744073709553664 to double
 ret double %r
}

define i128 @fptosi_float_power() {
 %r = fptosi float 0x43F0000000000000 to i128
 ret i128 %r
}

define i128 @fptosi_double_power() {
 %r = fptosi double 0x43F0000000000000 to i128
 ret i128 %r
}

define i128 @fptoui_float_power() {
 %r = fptoui float 0x43F0000000000000 to i128
 ret i128 %r
}

define i128 @fptoui_double_power() {
 %r = fptoui double 0x43F0000000000000 to i128
 ret i128 %r
}
