; RUN: llc -mtriple=mmix -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs -filetype=obj %s -o /dev/null
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %s -o /dev/null

target triple = "mmix"

declare <2 x i32> @word_pair_callee(<2 x i32>)
declare void @stack_vector_callee(
    i64, i64, i64, i64, i64, i64, i64, i64,
    i64, i64, i64, i64, i64, i64, i64, i64, <2 x i8>)

; Every contracted vector occupies one integer ABI register. These identity
; functions also establish the result path through $231.
; CHECK-LABEL: name: return_mask
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <8 x i1> @return_mask(<8 x i1> %value) {
  ret <8 x i1> %value
}

; CHECK-LABEL: name: return_byte
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <1 x i8> @return_byte(<1 x i8> %value) {
  ret <1 x i8> %value
}

; CHECK-LABEL: name: return_byte_pair
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <2 x i8> @return_byte_pair(<2 x i8> %value) {
  ret <2 x i8> %value
}

; CHECK-LABEL: name: return_half_pair
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <2 x i16> @return_half_pair(<2 x i16> %value) {
  ret <2 x i16> %value
}

; CHECK-LABEL: name: return_word_pair
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <2 x i32> @return_word_pair(<2 x i32> %value) {
  ret <2 x i32> %value
}

; CHECK-LABEL: name: return_float_pair
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <2 x float> @return_float_pair(<2 x float> %value) {
  ret <2 x float> %value
}

; CHECK-LABEL: name: return_double
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <1 x double> @return_double(<1 x double> %value) {
  ret <1 x double> %value
}

; Direct uninlined calls pass and receive the packed value through $231.
; CHECK-LABEL: name: call_direct
; CHECK:       $r231 = COPY %{{[0-9]+}}
; CHECK:       DIRECT_CALL_STATE @word_pair_callee
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
define <2 x i32> @call_direct(<2 x i32> %value) noinline {
  %result = call <2 x i32> @word_pair_callee(<2 x i32> %value)
  ret <2 x i32> %result
}

; Indirect calls use the same vector argument and result locations.
; CHECK-LABEL: name: call_indirect
; CHECK:       $r231 = COPY %{{[0-9]+}}
; CHECK:       CALL_STATE %{{[0-9]+}}, csr_mmix
; CHECK:       %{{[0-9]+}}:gpr64codegen = COPY $r231
define <2 x i32> @call_indirect(ptr %callee, <2 x i32> %value) noinline {
  %result = call <2 x i32> %callee(<2 x i32> %value)
  ret <2 x i32> %result
}

; The seventeenth argument occupies one complete stack slot after the sixteen
; argument registers. Its packed vector bits remain in the low slot bits.
; CHECK-LABEL: name: call_stack_vector
; CHECK:       ADJCALLSTACKDOWN 8, 0
; CHECK:       STOUI {{.*}}, 0 :: (store (s64) into stack)
; CHECK:       DIRECT_CALL_STATE @stack_vector_callee
; CHECK:       ADJCALLSTACKUP 8, 0
define void @call_stack_vector(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3,
    i64 %a4, i64 %a5, i64 %a6, i64 %a7,
    i64 %a8, i64 %a9, i64 %a10, i64 %a11,
    i64 %a12, i64 %a13, i64 %a14, i64 %a15, <2 x i8> %value) {
  call void @stack_vector_callee(
      i64 %a0, i64 %a1, i64 %a2, i64 %a3,
      i64 %a4, i64 %a5, i64 %a6, i64 %a7,
      i64 %a8, i64 %a9, i64 %a10, i64 %a11,
      i64 %a12, i64 %a13, i64 %a14, i64 %a15, <2 x i8> %value)
  ret void
}

; CHECK-LABEL: name: return_stack_vector
; CHECK:       fixedStack:
; CHECK:       offset: 0, size: 8, alignment: 8
; CHECK:       %{{[0-9]+}}:{{[^ ]+}} = LDOUI %fixed-stack.0, 0
; CHECK:       $r231 = COPY %{{[0-9]+}}
define <2 x i8> @return_stack_vector(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3,
    i64 %a4, i64 %a5, i64 %a6, i64 %a7,
    i64 %a8, i64 %a9, i64 %a10, i64 %a11,
    i64 %a12, i64 %a13, i64 %a14, i64 %a15, <2 x i8> %value) {
  ret <2 x i8> %value
}

; An advisory tail marker does not bypass fixed-vector ABI validation.
; CHECK-LABEL: name: conservative_tail
; CHECK:       DIRECT_CALL_STATE @word_pair_callee
; CHECK-NOT:   DIRECT_TAIL
; CHECK:       RET_VALUE
define <2 x i32> @conservative_tail(<2 x i32> %value) {
  %result = tail call <2 x i32> @word_pair_callee(<2 x i32> %value)
  ret <2 x i32> %result
}
