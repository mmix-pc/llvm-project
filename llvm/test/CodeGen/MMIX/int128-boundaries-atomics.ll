; RUN: split-file %s %t
; RUN: not llc -mtriple=mmix-unknown-linux -filetype=obj %t/rmw.ll -o %t.o 2>&1 | FileCheck %s --check-prefix=RMW --implicit-check-not="Stack dump" --implicit-check-not="LLVM ERROR"
; RUN: not llc -mtriple=mmix-unknown-linux -filetype=obj %t/cmpxchg.ll -o %t.o 2>&1 | FileCheck %s --check-prefix=CMPXCHG --implicit-check-not="Stack dump" --implicit-check-not="LLVM ERROR"
; RUN: not llc -mtriple=mmix-unknown-unknown -filetype=obj %t/rmw.ll -o %t.o 2>&1 | FileCheck %s --check-prefix=RMW --implicit-check-not="Stack dump" --implicit-check-not="LLVM ERROR"
; RUN: not llc -mtriple=mmix-unknown-unknown -filetype=obj %t/cmpxchg.ll -o %t.o 2>&1 | FileCheck %s --check-prefix=CMPXCHG --implicit-check-not="Stack dump" --implicit-check-not="LLVM ERROR"

; Native IR atomics must not acquire support from scalar i128 legalization.
; RMW: error: unsupported atomicrmw add: target supports atomics up to 8 bytes, but this atomic accesses 16 bytes
; CMPXCHG: error: unsupported cmpxchg: target supports atomics up to 8 bytes, but this atomic accesses 16 bytes

;--- rmw.ll
define i128 @rmw(ptr %p, i128 %value) {
  %old = atomicrmw add ptr %p, i128 %value monotonic
  ret i128 %old
}

;--- cmpxchg.ll
define i128 @compare(ptr %p, i128 %expected, i128 %desired) {
  %pair = cmpxchg ptr %p, i128 %expected, i128 %desired seq_cst seq_cst
  %old = extractvalue { i128, i1 } %pair, 0
  ret i128 %old
}
