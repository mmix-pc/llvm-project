; RUN: split-file %s %t
; RUN: not llc -mtriple=mmix -exception-model=sjlj %t/empty.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llc -mtriple=mmix -exception-model=wineh %t/empty.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MODEL
; RUN: not llc -mtriple=mmix -exception-model=dwarf %t/personality.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=PERSONALITY
; RUN: llc -mtriple=mmix-unknown-linux -filetype=obj %t/personality.ll -o %t.c.o
; RUN: not llc -mtriple=mmix-unknown-linux %t/foreign.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=LINUX-PERSONALITY
; RUN: not llc -mtriple=mmix -exception-model=dwarf %t/async.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ASYNC
; MODEL: MMIX supports only the DWARF exception model
; PERSONALITY: MMIX supports only the GNU C++ DWARF personality in function 'foreign_personality'
; LINUX-PERSONALITY: MMIX Linux supports only GNU C/C++ DWARF personalities in function 'foreign_personality'
; ASYNC: MMIX does not support asynchronous unwind tables in function 'async_frame'

;--- empty.ll
define void @empty() { ret void }

;--- personality.ll
declare i32 @__gcc_personality_v0(...)
define void @foreign_personality() personality ptr @__gcc_personality_v0 {
  ret void
}

;--- async.ll
define void @async_frame() uwtable(async) { ret void }

;--- foreign.ll
declare i32 @__gnat_personality_v0(...)
define void @foreign_personality() personality ptr @__gnat_personality_v0 {
  ret void
}
