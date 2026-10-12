; RUN: not llc -mtriple=mmix -filetype=asm %s -o %t.s 2>&1 | FileCheck %s
; RUN: test ! -s %t.s
; RUN: not llc -mtriple=mmix -filetype=obj %s -o %t.o 2>&1 | FileCheck %s
; RUN: test ! -s %t.o
; RUN: not llc -mtriple=mmix-unknown-linux -relocation-model=pic -filetype=obj %s -o %t.pic.o 2>&1 | FileCheck %s
; RUN: test ! -s %t.pic.o

; CHECK: LLVM ERROR: MMIX does not support nonzero address spaces in function 'load_nonzero_address_space'

@value = addrspace(1) global i64 0

define i64 @load_nonzero_address_space() {
  %value = load i64, ptr addrspace(1) @value
  ret i64 %value
}
