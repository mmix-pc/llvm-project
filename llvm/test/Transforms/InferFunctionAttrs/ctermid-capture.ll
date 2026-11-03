; RUN: opt -mtriple=x86_64-unknown-linux-gnu -passes='inferattrs,instcombine' -S %s | FileCheck %s
; RUN: opt -mtriple=x86_64-unknown-linux-gnu -passes='default<O2>' -S %s | FileCheck %s

; ctermid may return the supplied buffer. It cannot be captures(none), which
; would allow comparisons against an unescaped alloca to fold to false.
declare ptr @ctermid(ptr)

define i1 @caller_buffer() {
; CHECK-LABEL: define {{.*}}i1 @caller_buffer()
; CHECK: [[BUFFER:%.*]] = alloca
; CHECK: [[RESULT:%.*]] = {{.*}}call {{.*}}ptr @ctermid(ptr {{.*}}[[BUFFER]])
; CHECK: [[SAME:%.*]] = icmp eq ptr [[RESULT]], [[BUFFER]]
; CHECK: ret i1 [[SAME]]
  %buffer = alloca [32 x i8], align 1
  %result = call ptr @ctermid(ptr %buffer)
  %same = icmp eq ptr %result, %buffer
  ret i1 %same
}

; A null argument selects internal storage, so unconditional returned is not
; valid either. The return may also be null on failure.
define i1 @internal_buffer() {
; CHECK-LABEL: define {{.*}}i1 @internal_buffer()
; CHECK: [[RESULT:%.*]] = {{.*}}call {{.*}}ptr @ctermid(ptr {{.*}}null)
; CHECK: [[NULL:%.*]] = icmp eq ptr [[RESULT]], null
; CHECK: ret i1 [[NULL]]
  %result = call ptr @ctermid(ptr null)
  %isnull = icmp eq ptr %result, null
  ret i1 %isnull
}
