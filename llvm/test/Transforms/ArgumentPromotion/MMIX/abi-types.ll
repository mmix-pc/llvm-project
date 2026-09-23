; REQUIRES: mmix-registered-target
; RUN: opt -mtriple=mmix-unknown-linux -passes=argpromotion -S %s | FileCheck %s
; RUN: opt -mtriple=mmix-unknown-unknown -passes=argpromotion -S %s | FileCheck %s
; RUN: opt -mtriple=mmix-unknown-linux -passes='default<O3>' -S %s -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llc %t.ll -o /dev/null
; RUN: opt -mtriple=mmix-unknown-unknown -passes='default<O3>' -S %s | llc -o /dev/null

; Nonstandard wide loads are legal inside a function, but are not scalar ABI
; parameters. Keep pointers for them while still promoting supported integers.

; CHECK-LABEL: define internal fastcc i1 @read65(ptr
define internal fastcc i1 @read65(ptr %p) noinline {
  %v = load i65, ptr %p, align 1
  %r = icmp eq i65 %v, 0
  ret i1 %r
}

; CHECK-LABEL: define internal fastcc i1 @read104(ptr
define internal fastcc i1 @read104(ptr %p) noinline {
  %v = load i104, ptr %p, align 1
  %r = icmp eq i104 %v, 0
  ret i1 %r
}

; CHECK-LABEL: define internal fastcc i1 @read127(ptr
define internal fastcc i1 @read127(ptr %p) noinline {
  %v = load i127, ptr %p, align 1
  %r = icmp eq i127 %v, 0
  ret i1 %r
}

; CHECK-LABEL: define internal fastcc i1 @read64(i64
define internal fastcc i1 @read64(ptr %p) noinline {
  %v = load i64, ptr %p, align 8
  %r = icmp eq i64 %v, 0
  ret i1 %r
}

; CHECK-LABEL: define internal fastcc i1 @read128(i128
define internal fastcc i1 @read128(ptr %p) noinline {
  %v = load i128, ptr %p, align 8
  %r = icmp eq i128 %v, 0
  ret i1 %r
}

; CHECK-LABEL: define {{.*}}i1 @caller(
; CHECK: call fastcc i1 @read65(ptr
; CHECK: call fastcc i1 @read104(ptr
; CHECK: call fastcc i1 @read127(ptr
; CHECK: call fastcc i1 @read64(i64
; CHECK: call fastcc i1 @read128(i128
define i1 @caller(ptr %p) {
  %a = call fastcc i1 @read65(ptr %p)
  %b = call fastcc i1 @read104(ptr %p)
  %c = call fastcc i1 @read127(ptr %p)
  %d = call fastcc i1 @read64(ptr %p)
  %e = call fastcc i1 @read128(ptr %p)
  %ab = xor i1 %a, %b
  %cd = xor i1 %c, %d
  %abcd = xor i1 %ab, %cd
  %r = xor i1 %abcd, %e
  ret i1 %r
}

; Matching types alone must not bypass the base target-feature check.
; CHECK-LABEL: define internal fastcc i1 @read_with_system(ptr
define internal fastcc i1 @read_with_system(ptr %p) noinline "target-features"="+system" {
  %v = load i64, ptr %p, align 8
  %r = icmp eq i64 %v, 0
  ret i1 %r
}

; CHECK-LABEL: define {{.*}}i1 @different_features(
; CHECK: call fastcc i1 @read_with_system(ptr
define i1 @different_features(ptr %p) {
  %r = call fastcc i1 @read_with_system(ptr %p)
  ret i1 %r
}
