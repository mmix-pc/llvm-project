; RUN: not llc -mtriple=mmix-unknown-linux %s -o /dev/null 2>&1 | FileCheck %s
; RUN: llc -mtriple=mmix-unknown-unknown %s -o /dev/null
; CHECK: MMIX inline assembly may not clobber register 'r230' in an ordinary function
define void @clobber() {
  call void asm sideeffect "", "~{r230}"()
  ret void
}
