; RUN: split-file %s %t
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O0 -verify-machineinstrs -filetype=obj %t/supported.ll -o %t/pic.o0.o
; RUN: llvm-objdump --no-print-imm-hex -d %t/pic.o0.o | FileCheck %s --check-prefixes=PIC,PIC0
; RUN: llc -mtriple=mmix-unknown-linux -relocation-model=pic -O2 -verify-machineinstrs -filetype=obj %t/supported.ll -o %t/pic.o2.o
; RUN: llvm-objdump --no-print-imm-hex -d %t/pic.o2.o | FileCheck %s --check-prefix=PIC
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs %t/supported.ll -o - \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs -filetype=obj \
; RUN:   %t/supported.ll -o %t/supported.o
; RUN: llvm-objdump --no-print-imm-hex -d %t/supported.o \
; RUN:   | FileCheck %s --check-prefix=OBJ
; RUN: not llc -mtriple=mmix -filetype=asm %t/dynamic-stack.ll \
; RUN:   -o %t/dynamic-stack.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DYNAMIC-STACK
; RUN: test ! -s %t/dynamic-stack.s
; RUN: not llc -mtriple=mmix -filetype=obj %t/dynamic-stack.ll \
; RUN:   -o %t/dynamic-stack.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=DYNAMIC-STACK
; RUN: test ! -s %t/dynamic-stack.o
; RUN: not llc -mtriple=mmix -filetype=asm %t/realigned-stack.ll \
; RUN:   -o %t/realigned-stack.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=REALIGNED-STACK
; RUN: test ! -s %t/realigned-stack.s
; RUN: not llc -mtriple=mmix -filetype=asm %t/caller-copy.ll \
; RUN:   -o %t/caller-copy.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CALLER-COPY
; RUN: test ! -s %t/caller-copy.s
; RUN: not llc -mtriple=mmix -filetype=asm %t/incompatible-sret.ll \
; RUN:   -o %t/incompatible-sret.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=INCOMPATIBLE-SRET
; RUN: test ! -s %t/incompatible-sret.s
; RUN: not llc -mtriple=mmix -filetype=asm %t/unrestorable-frame.ll \
; RUN:   -o %t/unrestorable-frame.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=UNRESTORABLE-FRAME
; RUN: test ! -s %t/unrestorable-frame.s
; RUN: not llc -mtriple=mmix -filetype=asm %t/unsupported-cc.ll \
; RUN:   -o %t/unsupported-cc.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=UNSUPPORTED-CC
; RUN: test ! -s %t/unsupported-cc.s

; ASM-LABEL: required_direct:
; ASM-NOT: PUSH
; ASM: GETA {{r[0-9]+}}, %geta(direct_target)
; ASM: GO r255
; OBJ-LABEL: <required_direct>:
; OBJ: GO r255

; ASM-LABEL: required_indirect:
; ASM-NOT: PUSHGO
; ASM: GO r255
; OBJ-LABEL: <required_indirect>:
; OBJ: GO r255

; ASM-LABEL: required_recursive:
; ASM-NOT: PUSH
; ASM: JMPB required_recursive
; OBJ-LABEL: <required_recursive>:
; OBJ: JMPB

; ASM-LABEL: required_stack_arguments:
; ASM: LDO
; ASM: STOU
; ASM-NOT: PUSH
; ASM: GETA {{r[0-9]+}}, %geta(stack_target)
; ASM: GO r255
; OBJ-LABEL: <required_stack_arguments>:
; OBJ: GO r255

; ASM-LABEL: required_pair_result:
; ASM-NOT: PUSH
; ASM: GETA {{r[0-9]+}}, %geta(pair_target)
; ASM: GO r255
; OBJ-LABEL: <required_pair_result>:
; OBJ: GO r255

; PIC-LABEL: <required_direct>:
; PIC: GO r255,
; PIC-LABEL: <required_indirect>:
; PIC: GO r255,
; PIC-LABEL: <required_recursive>:
; PIC: GO r255,
; PIC-LABEL: <required_stack_arguments>:
; PIC0: SUBU r254, r254,
; PIC0: ADDU r254, r254,
; PIC: GO r255,
; PIC-LABEL: <required_pair_result>:
; PIC: GO r255,
; PIC-LABEL: <required_sret>:
; PIC: GO r255,
; PIC-LABEL: <required_fast_direct>:
; PIC: GO r255,
; PIC-LABEL: <required_fast_indirect>:
; PIC: GO r255,

; ASM-LABEL: required_sret:
; ASM-NOT: PUSH
; ASM: GO r255
; OBJ-LABEL: <required_sret>:
; OBJ: GO r255

; ASM-LABEL: required_fast_direct:
; ASM-NOT: PUSH
; ASM: GETA {{r[0-9]+}}, %geta(fast_target)
; ASM: GO r255
; OBJ-LABEL: <required_fast_direct>:
; OBJ: GO r255

; ASM-LABEL: required_fast_indirect:
; ASM-NOT: PUSHGO
; ASM: GO r255
; OBJ-LABEL: <required_fast_indirect>:
; OBJ: GO r255

; DYNAMIC-STACK: LLVM ERROR: MMIX required tail call is ineligible in function 'required_dynamic_stack': dynamic stack allocation prevents frame reuse
; REALIGNED-STACK: LLVM ERROR: MMIX required tail call is ineligible in function 'required_realigned_stack': stack realignment prevents frame reuse
; CALLER-COPY: LLVM ERROR: MMIX required tail call is ineligible in function 'required_caller_copy': caller-copy storage does not survive the transfer
; INCOMPATIBLE-SRET: LLVM ERROR: MMIX required tail call is ineligible in function 'required_incompatible_sret': indirect result is not forwarded compatibly
; UNRESTORABLE-FRAME: LLVM ERROR: MMIX required tail call is ineligible in function 'required_unrestorable_frame': software frame cannot be restored before transfer
; UNSUPPORTED-CC: LLVM ERROR: MMIX supports only C and Fast calling conventions in function 'required_unsupported_cc'

;--- supported.ll
target triple = "mmix-unknown-elf"

%pair = type { i32, i32 }
%wide = type { i64, i64 }

declare i64 @direct_target(i64)
declare i64 @stack_target(i64, i64, i64, i64, i64, i64, i64, i64, i64,
                          i64, i64, i64, i64, i64, i64, i64, i64)
declare %pair @pair_target(%pair)
declare void @sret_target(ptr sret(%wide), i64)
declare fastcc i64 @fast_target(i64)

define i64 @required_direct(i64 %value) {
  %result = musttail call i64 @direct_target(i64 %value)
  ret i64 %result
}

define i64 @required_indirect(ptr %callee, i64 %value) {
  %result = musttail call i64 %callee(ptr %callee, i64 %value)
  ret i64 %result
}

define i64 @required_recursive(i64 %value) {
  %result = musttail call i64 @required_recursive(i64 %value)
  ret i64 %result
}

define i64 @required_stack_arguments(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3, i64 %a4, i64 %a5, i64 %a6,
    i64 %a7, i64 %a8, i64 %a9, i64 %a10, i64 %a11, i64 %a12, i64 %a13,
    i64 %a14, i64 %a15, i64 %a16) {
  %result = musttail call i64 @stack_target(
      i64 %a16, i64 %a15, i64 %a14, i64 %a13, i64 %a12, i64 %a11,
      i64 %a10, i64 %a9, i64 %a8, i64 %a7, i64 %a6, i64 %a5, i64 %a4,
      i64 %a3, i64 %a2, i64 %a1, i64 %a0)
  ret i64 %result
}

define %pair @required_pair_result(%pair %value) {
  %result = musttail call %pair @pair_target(%pair %value)
  ret %pair %result
}

define void @required_sret(ptr sret(%wide) %result, i64 %value) {
  musttail call void @sret_target(ptr sret(%wide) %result, i64 %value)
  ret void
}

define fastcc i64 @required_fast_direct(i64 %value) {
  %result = musttail call fastcc i64 @fast_target(i64 %value)
  ret i64 %result
}

define fastcc i64 @required_fast_indirect(ptr %callee, i64 %value) {
  %result = musttail call fastcc i64 %callee(ptr %callee, i64 %value)
  ret i64 %result
}

;--- dynamic-stack.ll
target triple = "mmix-unknown-elf"

declare i64 @dynamic_target(i64)

define i64 @required_dynamic_stack(i64 %value) {
  %count = add i64 %value, 1
  %storage = alloca i8, i64 %count, align 8
  store volatile i8 0, ptr %storage, align 8
  %result = musttail call i64 @dynamic_target(i64 %value)
  ret i64 %result
}

;--- realigned-stack.ll
target triple = "mmix-unknown-elf"

declare i64 @realigned_target(i64)

define i64 @required_realigned_stack(i64 %value) {
  %storage = alloca i64, align 16
  store volatile i64 %value, ptr %storage, align 16
  %result = musttail call i64 @realigned_target(i64 %value)
  ret i64 %result
}

;--- caller-copy.ll
target triple = "mmix-unknown-elf"

%wide = type { i64, i64 }

declare void @caller_copy_target(ptr byval(%wide))

define void @required_caller_copy(ptr byval(%wide) %value) {
  musttail call void @caller_copy_target(ptr byval(%wide) %value)
  ret void
}

;--- incompatible-sret.ll
target triple = "mmix-unknown-elf"

%wide = type { i64, i64 }

declare void @incompatible_sret_target(ptr sret(%wide))

define void @required_incompatible_sret(ptr sret(%wide) %result) {
  %local = alloca %wide, align 8
  musttail call void @incompatible_sret_target(ptr sret(%wide) %local)
  ret void
}

;--- unrestorable-frame.ll
target triple = "mmix-unknown-elf"

declare i64 @unrestorable_target(i64)

define i64 @required_unrestorable_frame(i64 %value) #0 {
  %result = musttail call i64 @unrestorable_target(i64 %value)
  ret i64 %result
}

attributes #0 = { "probe-stack"="inline-asm" }

;--- unsupported-cc.ll
target triple = "mmix-unknown-elf"

declare preserve_mostcc i64 @unsupported_cc_target(i64)

define preserve_mostcc i64 @required_unsupported_cc(i64 %value) {
  %result = musttail call preserve_mostcc i64 @unsupported_cc_target(i64 %value)
  ret i64 %result
}
