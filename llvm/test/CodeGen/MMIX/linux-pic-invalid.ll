; RUN: split-file %s %t
; RUN: not llc -mtriple=mmix-unknown-linux -relocation-model=pic %t/nest.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=NEST
; RUN: not --crash llc -mtriple=mmix-unknown-unknown -relocation-model=pic %t/plain.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODE
; RUN: not --crash llc -mtriple=mmix-unknown-linux -relocation-model=pic --output-asm-variant=1 %t/plain.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODE
; NEST: MMIX does not support call arguments with unsupported ABI flags
; MODE: MMIX supports only the static relocation model
;--- nest.ll
declare void @nested(ptr nest)
define void @caller(ptr %p) {
  call void @nested(ptr nest %p)
  ret void
}
;--- plain.ll
define void @f() { ret void }
