; RUN: split-file %s %t
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs -filetype=obj %t/valid.ll -o %t/o0.o
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %t/valid.ll -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %t/valid.ll -o %t/o2.o
; RUN: not llc -mtriple=mmix -verify-machineinstrs %t/depth.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=DEPTH

; CHECK-LABEL: leaf:
; CHECK: GET r231, rJ
; CHECK: POP 0, 0

; The second GET captures the entry value, not the rJ written by PUSHGO.
; CHECK-LABEL: after_call:
; CHECK: GET r30, rJ
; CHECK: GET [[RA:r[0-9]+]], rJ
; CHECK: PUSHGO
; CHECK-NOT: GET {{r[0-9]+}}, rJ
; CHECK: OR r231, [[RA]], 0
; CHECK: POP 0, 0

; Both uses share one entry capture even though the last use is in another block.
; CHECK-LABEL: multiple_blocks:
; CHECK: GET r30, rJ
; CHECK: GET [[SAVED:r[0-9]+]], rJ
; CHECK: PUSHGO
; CHECK-NOT: GET {{r[0-9]+}}, rJ
; CHECK: POP 0, 0

; The capture must still be in the entry block when first requested later.
; CHECK-LABEL: later_block:
; CHECK: GET r30, rJ
; CHECK: GET {{r[0-9]+}}, rJ
; CHECK: PUSHGO
; CHECK-NOT: GET {{r[0-9]+}}, rJ
; CHECK: POP 0, 0

; CHECK-LABEL: fast:
; CHECK: GET {{r[0-9]+}}, rJ
; CHECK: PUSHGO
; CHECK: POP 0, 0
; CHECK-LABEL: variadic:
; CHECK: GET {{r[0-9]+}}, rJ
; CHECK: PUSHGO
; CHECK: POP 0, 0
; CHECK-LABEL: realigned:
; CHECK: GET {{r[0-9]+}}, rJ
; CHECK: PUSHGO
; CHECK: POP 0, 0
; CHECK-LABEL: tail:
; CHECK: GET r231, rJ
; CHECK-NOT: PUSH
; CHECK: GO r255

; DEPTH: MMIX supports only return address depth 0 in function 'outer'

;--- valid.ll
declare ptr @llvm.returnaddress.p0(i32 immarg)
declare void @clobber()
declare void @observe(ptr)
declare ptr @forward(ptr)

define ptr @leaf() {
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %ra
}

define ptr @after_call() {
  call void @clobber()
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %ra
}

define ptr @multiple_blocks(i1 %choose) {
  %before = call ptr @llvm.returnaddress.p0(i32 0)
  call void @observe(ptr %before)
  br i1 %choose, label %left, label %right
left:
  call void @clobber()
  %after = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %after
right:
  ret ptr %before
}

define ptr @later_block(i1 %choose) {
  call void @clobber()
  br i1 %choose, label %capture, label %other
capture:
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %ra
other:
  ret ptr null
}

define fastcc ptr @fast() {
  call void @clobber()
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %ra
}

define ptr @variadic(i64 %named, ...) {
  call void @clobber()
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %ra
}

define ptr @realigned() {
  %slot = alloca i64, align 64
  call void @observe(ptr %slot)
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  ret ptr %ra
}

define ptr @tail(ptr %unused) {
  %ra = call ptr @llvm.returnaddress.p0(i32 0)
  %result = musttail call ptr @forward(ptr %ra)
  ret ptr %result
}

;--- depth.ll
declare ptr @llvm.returnaddress.p0(i32 immarg)
define ptr @outer() {
  %ra = call ptr @llvm.returnaddress.p0(i32 1)
  ret ptr %ra
}
