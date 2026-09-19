; Atomic loads must remain read-only and retain their width and ordering.
; RUN: llc -mtriple=mmix-unknown-linux -O0 -verify-machineinstrs %s -o - | FileCheck %s --implicit-check-not=CSWAP
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs %s -o - | FileCheck %s --implicit-check-not=CSWAP
; RUN: llc -mtriple=mmix-unknown-unknown -O0 -verify-machineinstrs %s -o - | FileCheck %s --implicit-check-not=CSWAP
; RUN: llc -mtriple=mmix-unknown-unknown -O2 -verify-machineinstrs %s -o - | FileCheck %s --implicit-check-not=CSWAP
; RUN: llc -mtriple=mmix-unknown-linux -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=MMO

; CHECK-LABEL: load_8_monotonic:
; CHECK-NOT: SYNC
; CHECK: LDBU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK-NOT: SYNC
; CHECK: POP
; MMO-LABEL: name: load_8_monotonic
; MMO: LDBUI {{.*}} :: (volatile load monotonic (s8) from %ir.p)
define i8 @load_8_monotonic(ptr %p) {
  %v = load atomic volatile i8, ptr %p monotonic, align 1
  ret i8 %v
}

; CHECK-LABEL: load_8_acquire:
; CHECK-NOT: SYNC
; CHECK: LDBU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_8_acquire
; MMO: LDBUI {{.*}} :: (volatile load monotonic (s8) from %ir.p)
define i8 @load_8_acquire(ptr %p) {
  %v = load atomic volatile i8, ptr %p acquire, align 1
  ret i8 %v
}

; CHECK-LABEL: load_8_seq_cst:
; CHECK: SYNC 3
; CHECK: LDBU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_8_seq_cst
; MMO: LDBUI {{.*}} :: (volatile load monotonic (s8) from %ir.p)
define i8 @load_8_seq_cst(ptr %p) {
  %v = load atomic volatile i8, ptr %p seq_cst, align 1
  ret i8 %v
}

; CHECK-LABEL: load_16_monotonic:
; CHECK-NOT: SYNC
; CHECK: LDWU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK-NOT: SYNC
; CHECK: POP
; MMO-LABEL: name: load_16_monotonic
; MMO: LDWUI {{.*}} :: (volatile load monotonic (s16) from %ir.p)
define i16 @load_16_monotonic(ptr %p) {
  %v = load atomic volatile i16, ptr %p monotonic, align 2
  ret i16 %v
}

; CHECK-LABEL: load_16_acquire:
; CHECK-NOT: SYNC
; CHECK: LDWU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_16_acquire
; MMO: LDWUI {{.*}} :: (volatile load monotonic (s16) from %ir.p)
define i16 @load_16_acquire(ptr %p) {
  %v = load atomic volatile i16, ptr %p acquire, align 2
  ret i16 %v
}

; CHECK-LABEL: load_16_seq_cst:
; CHECK: SYNC 3
; CHECK: LDWU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_16_seq_cst
; MMO: LDWUI {{.*}} :: (volatile load monotonic (s16) from %ir.p)
define i16 @load_16_seq_cst(ptr %p) {
  %v = load atomic volatile i16, ptr %p seq_cst, align 2
  ret i16 %v
}

; CHECK-LABEL: load_32_monotonic:
; CHECK-NOT: SYNC
; CHECK: LDTU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK-NOT: SYNC
; CHECK: POP
; MMO-LABEL: name: load_32_monotonic
; MMO: LDTUI {{.*}} :: (volatile load monotonic (s32) from %ir.p)
define i32 @load_32_monotonic(ptr %p) {
  %v = load atomic volatile i32, ptr %p monotonic, align 4
  ret i32 %v
}

; CHECK-LABEL: load_32_acquire:
; CHECK-NOT: SYNC
; CHECK: LDTU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_32_acquire
; MMO: LDTUI {{.*}} :: (volatile load monotonic (s32) from %ir.p)
define i32 @load_32_acquire(ptr %p) {
  %v = load atomic volatile i32, ptr %p acquire, align 4
  ret i32 %v
}

; CHECK-LABEL: load_32_seq_cst:
; CHECK: SYNC 3
; CHECK: LDTU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_32_seq_cst
; MMO: LDTUI {{.*}} :: (volatile load monotonic (s32) from %ir.p)
define i32 @load_32_seq_cst(ptr %p) {
  %v = load atomic volatile i32, ptr %p seq_cst, align 4
  ret i32 %v
}

; CHECK-LABEL: load_64_monotonic:
; CHECK-NOT: SYNC
; CHECK: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK-NOT: SYNC
; CHECK: POP
; MMO-LABEL: name: load_64_monotonic
; MMO: LDOUI {{.*}} :: (volatile load monotonic (s64) from %ir.p)
define i64 @load_64_monotonic(ptr %p) {
  %v = load atomic volatile i64, ptr %p monotonic, align 8
  ret i64 %v
}

; CHECK-LABEL: load_64_acquire:
; CHECK-NOT: SYNC
; CHECK: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_64_acquire
; MMO: LDOUI {{.*}} :: (volatile load monotonic (s64) from %ir.p)
define i64 @load_64_acquire(ptr %p) {
  %v = load atomic volatile i64, ptr %p acquire, align 8
  ret i64 %v
}

; CHECK-LABEL: load_64_seq_cst:
; CHECK: SYNC 3
; CHECK: LDOU {{r[0-9]+}}, {{r[0-9]+}}, 0
; CHECK: SYNC 3
; CHECK: POP
; MMO-LABEL: name: load_64_seq_cst
; MMO: LDOUI {{.*}} :: (volatile load monotonic (s64) from %ir.p)
define i64 @load_64_seq_cst(ptr %p) {
  %v = load atomic volatile i64, ptr %p seq_cst, align 8
  ret i64 %v
}

; Extension combines must not lose the atomic read or introduce a write.
; CHECK-LABEL: load_signed_byte:
; CHECK: LDB
; CHECK: POP
define i64 @load_signed_byte(ptr %p) {
  %v = load atomic i8, ptr %p monotonic, align 1
  %wide = sext i8 %v to i64
  ret i64 %wide
}

; Floating atomic reads use integer memory operations, not FP conversion loads.
; CHECK-LABEL: load_float:
; CHECK: LDTU
; CHECK: SYNC 3
; CHECK: POP
define float @load_float(ptr %p) {
  %v = load atomic float, ptr %p acquire, align 4
  ret float %v
}

; CHECK-LABEL: load_double:
; CHECK: LDOU
; CHECK: SYNC 3
; CHECK: POP
define double @load_double(ptr %p) {
  %v = load atomic double, ptr %p acquire, align 8
  ret double %v
}
