; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs -filetype=obj %s -o %t.o

; ISEL-LABEL: name: identity
; ISEL: liveins: $r231, $r232
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @identity(i128 %v) { ret i128 %v }

; ISEL-LABEL: name: fast_identity
; ISEL: liveins: $r231, $r232
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define fastcc i128 @fast_identity(i128 %v) { ret i128 %v }

; ISEL-LABEL: name: exhausted_15
; ISEL: fixedStack:
; ISEL-DAG: offset: 0, size: 8, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @exhausted_15(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i128 %v) { ret i128 %v }

; ISEL-LABEL: name: exhausted_16
; ISEL: fixedStack:
; ISEL-DAG: offset: 0, size: 8, alignment: 8
; ISEL-DAG: offset: 8, size: 8, alignment: 8
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @exhausted_16(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i128 %v) { ret i128 %v }

; ISEL-LABEL: name: indirect
; ISEL: CALL_STATE {{.*}}implicit $r231, implicit $r232
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @indirect(ptr %fn, i128 %v) {
 %r = call i128 %fn(i128 %v)
 ret i128 %r
}

; ISEL-LABEL: name: tail_identity
; ISEL: TAIL
define i128 @tail_identity(i128 %v) {
 %r = musttail call i128 @identity(i128 %v)
 ret i128 %r
}

; ISEL-LABEL: name: tail_fast
; ISEL: TAIL
define fastcc i128 @tail_fast(i128 %v) {
 %r = musttail call fastcc i128 @fast_identity(i128 %v)
 ret i128 %r
}

; The aggregate still arrives by caller-copy pointer, not by scalar slots.
; ISEL-LABEL: name: aggregate_copy
; ISEL: liveins: $r231
; ISEL: RET_PAIR {{.*}}implicit $r231, implicit $r232
define i128 @aggregate_copy(ptr byval({ i128 }) align 8 %p) {
 %v = load i128, ptr %p, align 8
 ret i128 %v
}

; The aggregate hidden result still consumes r251, not an ordinary slot.
; ISEL-LABEL: name: aggregate_result
; ISEL: liveins: $r251, $r231, $r232
; ISEL: RET_VALUE {{.*}}implicit $r231
define void @aggregate_result(ptr sret({ i128 }) align 8 %p, i128 %v) {
 store i128 %v, ptr %p, align 8
 ret void
}

; Two known words at a caller's register-to-stack boundary are not swapped.
; ISEL-LABEL: name: call_exhausted
; ISEL: ADJCALLSTACKDOWN 8, 0
; ISEL: [[LOW:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 2
; ISEL: STOUI {{(killed )?}}[[LOW]], {{.*}}, 0 :: (store (s64) into stack)
; ISEL: [[HIGH:%[0-9]+]]:{{[^ ]+}} = LOAD_IMM64 1
; ISEL: $r246 = COPY [[HIGH]]
; ISEL: CALL_STATE @exhausted_15
define i128 @call_exhausted() {
 %r = call i128 @exhausted_15(i64 0, i64 0, i64 0, i64 0, i64 0,
   i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0, i64 0,
   i64 0, i128 18446744073709551618)
 ret i128 %r
}
