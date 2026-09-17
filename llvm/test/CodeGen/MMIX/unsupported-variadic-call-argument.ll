; RUN: split-file %s %t
; RUN: not llc -mtriple=mmix -stop-after=mmix-isel \
; RUN:   %t/narrow.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NARROW
; RUN: not llc -mtriple=mmix -stop-after=mmix-isel \
; RUN:   %t/float.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLOAT
; RUN: not llc -mtriple=mmix -stop-after=mmix-isel \
; RUN:   %t/i32-flags.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=I32

; NARROW: LLVM ERROR: MMIX requires variadic integer call arguments narrower than i32 to carry exactly one of signext or zeroext in function 'narrow'
; FLOAT: LLVM ERROR: MMIX requires variadic float call arguments to be promoted to double in function 'short_float'
; I32: LLVM ERROR: MMIX requires variadic i32 call arguments to carry exactly one of signext or zeroext in function 'missing_extension'

;--- narrow.ll
declare void @variadic(...)

define void @narrow(i8 %value) {
  call void (...) @variadic(i8 %value)
  ret void
}

;--- float.ll
declare void @variadic(...)

define void @short_float(float %value) {
  call void (...) @variadic(float %value)
  ret void
}

;--- i32-flags.ll
declare void @variadic(...)

define void @missing_extension(i32 %value) {
  call void (...) @variadic(i32 %value)
  ret void
}
