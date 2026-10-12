; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-linux -O2 -relocation-model=pic -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=OBJECT
; RUN: llc -mtriple=mmix-unknown-linux -O2 -stop-after=finalize-isel -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=MIR

; A branch must neither push/pop a window nor run a tail epilogue. Values and
; PHIs stay live over the computed edge, including the backwards edge below.
; CHECK-LABEL: dispatch:
; CHECK-NOT: PUSH
; CHECK-NOT: POP
; CHECK: GO r255, {{r[0-9]+}}, 0
; OBJECT: GO r255, {{r[0-9]+}}, 0x0
; MIR: PseudoIndirectBranch {{(killed )?}}%{{[0-9]+}}, implicit-def dead $r255

@labels = internal constant [2 x ptr] [ptr blockaddress(@dispatch, %add), ptr blockaddress(@dispatch, %subtract)], align 8

define i64 @dispatch(i64 %op, i64 %value) {
entry:
  %index = and i64 %op, 1
  %slot = getelementptr [2 x ptr], ptr @labels, i64 0, i64 %index
  %target = load ptr, ptr %slot, align 8
  indirectbr ptr %target, [label %add, label %subtract]
add:
  %sum = add i64 %value, 7
  ret i64 %sum
subtract:
  %difference = sub i64 %value, 9
  ret i64 %difference
}

; CHECK-LABEL: loop:
; CHECK: GO r255, {{r[0-9]+}}, 0
define i64 @loop(i64 %count, i64 %value) {
entry:
  br label %body
body:
  %n = phi i64 [ %count, %entry ], [ %next, %body ]
  %v = phi i64 [ %value, %entry ], [ %sum, %body ]
  %sum = add i64 %v, %n
  %next = sub i64 %n, 1
  %finished = icmp eq i64 %next, 0
  %target = select i1 %finished, ptr blockaddress(@loop, %exit), ptr blockaddress(@loop, %body)
  indirectbr ptr %target, [label %body, label %exit]
exit:
  ret i64 %sum
}
