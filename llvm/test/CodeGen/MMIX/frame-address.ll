; RUN: split-file %s %t
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %t/valid.ll -o %t/o0.s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %t/valid.ll -o %t/o2.s
; RUN: FileCheck %s < %t/o0.s
; RUN: FileCheck %s < %t/o2.s
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs -filetype=obj %t/valid.ll -o %t/o0.o
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %t/valid.ll -o %t/o2.o
; RUN: not llc -mtriple=mmix -verify-machineinstrs %t/depth.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=DEPTH
; RUN: not llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %t/depth.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=DEPTH
; DEPTH: MMIX supports only frame address depth 0 in function 'outer'

; Taking the frame address forces FP even in an otherwise frameless leaf.
; Capture entry SP after preserving the caller's FP, and return before restoring it.
; CHECK-LABEL: leaf:
; CHECK: SUBU r254, r254, 8
; CHECK: STOU r253, r254, 0
; CHECK: ADDU r253, r254, 8
; CHECK: .cfi_def_cfa r253, 0
; CHECK: OR r231, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: ADDU r254, r254, 8
; CHECK: POP 0, 0

; CHECK-LABEL: fixed:
; CHECK: SUBU r254, r254, [[SIZE:[0-9]+]]
; CHECK: STOU r253, r254,
; CHECK: ADDU r253, r254, [[SIZE]]
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; Changing SP must not change the value returned by frameaddress.
; CHECK-LABEL: dynamic:
; CHECK: ADDU r253, r254,
; CHECK: SUBU {{r[0-9]+}}, {{r[0-9]+}}, {{r[0-9]+}}
; CHECK: OR r254,
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: OR r254, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; FP is established before alignment, not from the rounded-down SP.
; CHECK-LABEL: realigned:
; CHECK: ADDU r253, r254,
; CHECK: ANDN r254, r254, 63
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: OR r254, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; BP owns the realigned local area, but the intrinsic still returns FP.
; CHECK-LABEL: realigned_dynamic:
; CHECK: ADDU r253, r254,
; CHECK: ANDN r254, r254, 63
; CHECK: OR r29, r254, 0
; CHECK: PUSHGO
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: OR r254, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; A use first encountered in a successor still requests an entry prologue.
; CHECK-LABEL: later_block:
; CHECK: STOU r253, r254,
; CHECK: ADDU r253, r254,
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; CHECK-LABEL: fast:
; CHECK: ADDU r253, r254,
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; The variadic save area changes the old-FP slot, not the intrinsic's base.
; CHECK-LABEL: variadic:
; CHECK: SUBU r254, r254, [[VAR_SIZE:[0-9]+]]
; CHECK: ADDU r253, r254, [[VAR_SIZE]]
; CHECK: PUSHGO
; CHECK: OR r231, r253, 0
; CHECK: LDOU r253, r253, r255
; CHECK: POP 0, 0

; CHECK-LABEL: plain:
; CHECK-NOT: r253
; CHECK: POP 0, 0

;--- valid.ll
declare ptr @llvm.frameaddress.p0(i32 immarg)
declare void @observe(ptr)
declare void @clobber()

define ptr @leaf() {
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @fixed() {
  %slot = alloca i64, align 8
  call void @observe(ptr %slot)
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @dynamic(i64 %size) {
  %slot = alloca i8, i64 %size, align 8
  call void @observe(ptr %slot)
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @realigned() {
  %slot = alloca i64, align 64
  call void @observe(ptr %slot)
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @realigned_dynamic(i64 %size) {
  %aligned = alloca i64, align 64
  %slot = alloca i8, i64 %size, align 8
  call void @observe(ptr %aligned)
  call void @observe(ptr %slot)
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @later_block(i1 %choose) {
  call void @clobber()
  br i1 %choose, label %capture, label %other
capture:
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
other:
  ret ptr null
}

define fastcc ptr @fast() {
  call void @clobber()
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @variadic(i64 %named, ...) {
  call void @clobber()
  %frame = call ptr @llvm.frameaddress.p0(i32 0)
  ret ptr %frame
}

define ptr @plain(ptr %value) {
  ret ptr %value
}

;--- depth.ll
declare ptr @llvm.frameaddress.p0(i32 immarg)
define ptr @outer() {
  %frame = call ptr @llvm.frameaddress.p0(i32 1)
  ret ptr %frame
}
