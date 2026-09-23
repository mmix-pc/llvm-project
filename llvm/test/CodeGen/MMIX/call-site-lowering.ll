; RUN: llc -mtriple=mmix -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix -verify-machineinstrs -stop-after=prolog-epilog %s -o - | FileCheck %s --check-prefix=PEI
; RUN: llc -mtriple=mmix -O0 -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mmix -O2 -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-objdump --no-print-imm-hex -dr %t.o | FileCheck %s --check-prefix=OBJ

target triple = "mmix"

declare void @void_callee()
declare i64 @i64_callee(i64)
declare signext i8 @sext_callee(i8 signext)
declare zeroext i32 @zext_callee(i32 zeroext)
declare float @f32_callee(float)
declare i16 @anyext_callee(i16)
declare extern_weak void @weak_callee()
declare double @many_callee(i64, i64, i64, i64, i64, i64, i64, i64,
                            i64, i64, i64, i64, i64, i64, i64, i64,
                            i64, double)

; ISEL-LABEL: name: call_void
; ISEL:       ADJCALLSTACKDOWN 0, 0
; ISEL-NEXT:  [[VOID_SCRATCH:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @void_callee
; ISEL-NEXT:  DIRECT_CALL_STATE @void_callee, killed [[VOID_SCRATCH]], csr_mmix{{.*}}implicit $r254
; ISEL-NEXT:  ADJCALLSTACKUP 0, 0
define void @call_void() {
  call void @void_callee()
  ret void
}

; The argument is copied to r231 before the call and the result is copied
; from the same documented result register after it.
; ISEL-LABEL: name: call_i64
; ISEL:       $r231 = COPY %{{[0-9]+}}
; ISEL-NEXT:  DIRECT_CALL_STATE @i64_callee, {{.*}}csr_mmix{{.*}}implicit $r254{{.*}}implicit $r231
; ISEL-NEXT:  ADJCALLSTACKUP 0, 0
; ISEL-NEXT:  %{{[0-9]+}}:gpr64codegen = COPY $r231
define i64 @call_i64(i64 %value) {
  %result = call i64 @i64_callee(i64 %value)
  ret i64 %result
}

; Narrow signed and unsigned values use the same extension assignments as
; formal arguments and returns.
; ISEL-LABEL: name: call_signext
; ISEL:       [[SHIFTED:%[0-9]+]]:{{[^ ]+}} = SLUI {{.*}}, 56
; ISEL-NEXT:  [[SEXT:%[0-9]+]]:{{[^ ]+}} = SRI killed [[SHIFTED]], 56
; ISEL:       $r231 = COPY [[SEXT]]
; ISEL:       DIRECT_CALL_STATE @sext_callee, {{.*}}csr_mmix{{.*}}implicit $r254{{.*}}implicit $r231
; OBJ-LABEL: <call_signext>:
; OBJ:       SLU r250, r231, 56
; OBJ-NEXT:  SR r231, r250, 56
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
define i64 @call_signext(i64 %value) {
  %narrow = trunc i64 %value to i8
  %result = call signext i8 @sext_callee(i8 signext %narrow)
  %wide = sext i8 %result to i64
  ret i64 %wide
}

; ISEL-LABEL: name: call_zeroext
; ISEL:       [[ZEXT:%[0-9]+]]:{{[^ ]+}} = AND
; ISEL:       $r231 = COPY [[ZEXT]]
; ISEL:       DIRECT_CALL_STATE @zext_callee, {{.*}}csr_mmix{{.*}}implicit $r254{{.*}}implicit $r231
; OBJ-LABEL: <call_zeroext>:
; OBJ:       SETL r250, 65535
; OBJ-NEXT:  INCML r250, 65535
; OBJ-NEXT:  AND r231, r231, r250
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
define i64 @call_zeroext(i64 %value) {
  %narrow = trunc i64 %value to i32
  %result = call zeroext i32 @zext_callee(i32 zeroext %narrow)
  %wide = zext i32 %result to i64
  ret i64 %wide
}

; f32 call slots carry the short-float bit representation in an i64 location.
; ISEL-LABEL: name: call_f32
; ISEL:       $r231 = COPY %{{[0-9]+}}
; ISEL:       DIRECT_CALL_STATE @f32_callee, {{.*}}csr_mmix{{.*}}implicit $r254{{.*}}implicit $r231
; ISEL:       %{{[0-9]+}}:gpr64codegen = COPY $r231
define float @call_f32(float %value) {
  %result = call float @f32_callee(float %value)
  ret float %result
}

; An unattributed raw LLVM narrow argument uses AExt. The caller preserves the
; declared low bits without inventing C signedness, and the unextended result
; is masked only when its consumer requests a zero extension.
; ISEL-LABEL: name: call_anyext
; ISEL:       $r231 = COPY %{{[0-9]+}}
; ISEL:       DIRECT_CALL_STATE @anyext_callee, {{.*}}implicit $r231
; ISEL:       [[RESULT:%[0-9]+]]:{{[^ ]+}} = COPY $r231
; ISEL:       [[MASK:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 65535
; ISEL-NEXT:  %{{[0-9]+}}:{{[^ ]+}} = AND [[RESULT]], killed [[MASK]]
; OBJ-LABEL: <call_anyext>:
; OBJ:       PUSHGO r31, {{r[0-9]+}}, 0
; OBJ:       SETL r250, 65535
; OBJ-NEXT:  AND r231, r231, r250
define i64 @call_anyext(i64 %value) {
  %narrow = trunc i64 %value to i16
  %result = call i16 @anyext_callee(i16 %narrow)
  %wide = zext i16 %result to i64
  ret i64 %wide
}

; The first sixteen slots use r231-r246. Later slots occupy consecutive octas
; in the outgoing area, including f64 values stored by their i64 bit pattern.
; ISEL-LABEL: name: call_with_stack_arguments
; ISEL:       ADJCALLSTACKDOWN 16, 0
; ISEL:       STOUI {{.*}}, 8 :: (store (s64) into stack + 8)
; ISEL:       STOUI {{.*}}, 0 :: (store (s64) into stack)
; ISEL:       DIRECT_CALL_STATE @many_callee, {{.*}}csr_mmix{{.*}}implicit $r254{{.*}}implicit $r231, implicit $r232, implicit $r233, implicit $r234, implicit $r235, implicit $r236, implicit $r237, implicit $r238, implicit $r239, implicit $r240, implicit $r241, implicit $r242, implicit $r243, implicit $r244, implicit $r245, implicit $r246
; ISEL-NEXT:  ADJCALLSTACKUP 16, 0
; PEI-LABEL: name: call_with_stack_arguments
; PEI:       stackSize: 16
; PEI:       maxCallFrameSize: 16
; PEI:       $r254 = frame-setup SUBUI $r254, 16
; PEI-NOT:   ADJCALLSTACK
; PEI:       DIRECT_CALL_STATE @many_callee, {{.*}}, csr_mmix
define double @call_with_stack_arguments() nounwind {
  %result = call double @many_callee(
      i64 0, i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7,
      i64 8, i64 9, i64 10, i64 11, i64 12, i64 13, i64 14, i64 15,
      i64 16, double 1.0)
  ret double %result
}

; Procedure instruction selection happens after register allocation, so an
; indirect callee is still neutral at the SelectionDAG instruction stage.
; ISEL-LABEL: name: call_indirect
; ISEL-NOT:   LOAD_CALL_ADDR
; ISEL:       CALL_STATE {{.*}}csr_mmix{{.*}}implicit $r254{{.*}}implicit $r231
define i64 @call_indirect(ptr %callee, i64 %value) {
  %result = call i64 %callee(i64 %value)
  ret i64 %result
}

; Eligible ordinary C tail calls select the terminal direct-call state.
; ISEL-LABEL: name: tail_call_enabled
; ISEL:       hasTailCall: true
; ISEL:       MATERIALIZED_DIRECT_TAIL_STATE @i64_callee, {{.*}}csr_mmix
define i64 @tail_call_enabled(i64 %value) {
  %result = tail call i64 @i64_callee(i64 %value)
  ret i64 %result
}

; Weak, explicitly sectioned, and externally visible preemptable definitions
; retain both their callee identity and the text fallback scratch register.
; ISEL-LABEL: name: call_weak
; ISEL:       [[WEAK_SCRATCH:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @weak_callee
; ISEL:       DIRECT_CALL_STATE @weak_callee, killed [[WEAK_SCRATCH]], csr_mmix
define void @call_weak() {
  call void @weak_callee()
  ret void
}

define internal void @section_callee() section ".calls" {
  ret void
}

; ISEL-LABEL: name: call_other_section
; ISEL:       [[SECTION_SCRATCH:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @section_callee
; ISEL:       DIRECT_CALL_STATE @section_callee, killed [[SECTION_SCRATCH]], csr_mmix
define void @call_other_section() {
  call void @section_callee()
  ret void
}

define dso_preemptable void @visible_callee() {
  ret void
}

; ISEL-LABEL: name: call_visible
; ISEL:       [[VISIBLE_SCRATCH:%[0-9]+]]:{{[^ ]+}} = LOAD_CALL_ADDR @visible_callee
; ISEL:       DIRECT_CALL_STATE @visible_callee, killed [[VISIBLE_SCRATCH]], csr_mmix
define void @call_visible() {
  call void @visible_callee()
  ret void
}

define internal void @stable_callee() {
  ret void
}

; A stable same-section local target remains a direct layout-selected call.
; ISEL-LABEL: name: call_stable
; ISEL-NOT:   LOAD_CALL_ADDR
; ISEL:       CALL_STATE @stable_callee, csr_mmix
define void @call_stable() {
  call void @stable_callee()
  ret void
}
