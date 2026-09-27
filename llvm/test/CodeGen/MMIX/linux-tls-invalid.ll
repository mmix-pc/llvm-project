; RUN: split-file %s %t
; RUN: not llc -mtriple=mmix-unknown-linux %t/external.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llc -mtriple=mmix-unknown-linux %t/initialexec.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llc -mtriple=mmix-unknown-linux %t/localdynamic.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llc -mtriple=mmix-unknown-linux %t/pie.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=PIE
; RUN: not --crash llc -mtriple=mmix-unknown-linux -relocation-model=pic %t/local.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=PIC
; RUN: not llc -mtriple=mmix-unknown-unknown %t/local.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=GENERIC
; RUN: not llc -mtriple=mmix-unknown-unknown %t/tp.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=GENERIC
; RUN: not llc -mtriple=mmix-unknown-unknown --output-asm-variant=1 %t/mmixal.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MMIXAL
; MODEL: MMIX Linux supports only the effective local-exec TLS model
; PIE: MMIX Linux TLS requires static non-PIE code
; PIC: MMIX supports only the static relocation model
; GENERIC: MMIX does not support thread-local storage in function
; MMIXAL: MMIXAL output variant 1 does not support thread-local symbol 'x'

;--- external.ll
@x = external thread_local global i64
define ptr @address() { ret ptr @x }
;--- initialexec.ll
@x = external thread_local(initialexec) global i64
define ptr @address() { ret ptr @x }
;--- localdynamic.ll
@x = external thread_local(localdynamic) global i64
define ptr @address() { ret ptr @x }
;--- local.ll
@x = thread_local(localexec) global i64 1
define ptr @address() { ret ptr @x }
;--- pie.ll
@x = thread_local(localexec) global i64 1
define ptr @address() { ret ptr @x }
!llvm.module.flags = !{!0}
!0 = !{i32 7, !"PIE Level", i32 2}
;--- mmixal.ll
@x = thread_local(localexec) global i64 1
define void @Main() {
  br label %loop
loop:
  br label %loop
}
;--- tp.ll
declare ptr @llvm.thread.pointer()
define ptr @tp() {
  %p = call ptr @llvm.thread.pointer()
  ret ptr %p
}
