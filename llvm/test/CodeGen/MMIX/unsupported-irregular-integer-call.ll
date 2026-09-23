; REQUIRES: mmix-registered-target
; RUN: not llc -mtriple=mmix-unknown-linux %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mmix-unknown-unknown %s -o /dev/null 2>&1 | FileCheck %s

; CHECK: MMIX does not support ABI type 'i104' for call arguments in function 'caller'

declare fastcc i1 @callee(i104)

define i1 @caller(ptr %p) {
  %v = load i104, ptr %p, align 1
  %r = call fastcc i1 @callee(i104 %v)
  ret i1 %r
}
