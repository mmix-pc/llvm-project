; RUN: llc -mtriple=mmix -verify-machineinstrs -filetype=asm %s -o - | FileCheck %s
; RUN: llc -mtriple=mmix -verify-machineinstrs -stop-after=postrapseudos %s -o - | FileCheck %s --check-prefix=RP

target triple = "mmix"

; These raw LLVM IR operations test a backend capability. They do not define
; or imply a source-level C _Atomic ABI.

; CHECK-LABEL: cmp_monotonic:
; CHECK-NOT:   SYNC
; CHECK:       PUT rP, r232
; CHECK-NEXT:  OR r255, r233, 0
; CHECK-NEXT:  CSWAP r255, r231, 0
; CHECK-NEXT:  GET r231, rP
; CHECK-NOT:   SYNC
; CHECK:       POP 0, 0
; RP-LABEL: name: cmp_monotonic
; RP:       $rp = PUT {{(killed )?}}$r232
; RP-NEXT:  $r255 = ORI killed $r233, 0
; RP-NEXT:  $r255 = CSWAPI killed $r255, {{(killed )?}}$r231, 0, implicit-def $rp, implicit $rp :: (load store monotonic monotonic (s64) on %ir.p)
; RP-NEXT:  $r231 = GET $rp
define i64 @cmp_monotonic(ptr %p, i64 %expected, i64 %new) {
  %pair = cmpxchg ptr %p, i64 %expected, i64 %new monotonic monotonic
  %old = extractvalue { i64, i1 } %pair, 0
  ret i64 %old
}

; CHECK-LABEL: cmp_acquire:
; CHECK-NOT:   SYNC
; CHECK:       CSWAP
; CHECK:       GET
; CHECK:       SYNC 3
; CHECK-NEXT:  POP 0, 0
define i1 @cmp_acquire(ptr %p, i64 %expected, i64 %new) nounwind {
  %pair = cmpxchg ptr %p, i64 %expected, i64 %new acquire acquire
  %ok = extractvalue { i64, i1 } %pair, 1
  ret i1 %ok
}

; CHECK-LABEL: cmp_release:
; CHECK:       SYNC 3
; CHECK:       CSWAP
; CHECK-NOT:   SYNC
; CHECK:       POP 0, 0
define i1 @cmp_release(ptr %p, i64 %expected, i64 %new) {
  %pair = cmpxchg ptr %p, i64 %expected, i64 %new release monotonic
  %ok = extractvalue { i64, i1 } %pair, 1
  ret i1 %ok
}

; CHECK-LABEL: cmp_acqrel:
; CHECK:       SYNC 3
; CHECK:       CSWAP
; CHECK:       SYNC 3
; CHECK-NEXT:  POP 0, 0
define i1 @cmp_acqrel(ptr %p, i64 %expected, i64 %new) nounwind {
  %pair = cmpxchg ptr %p, i64 %expected, i64 %new acq_rel acquire
  %ok = extractvalue { i64, i1 } %pair, 1
  ret i1 %ok
}

; CHECK-LABEL: cmp_seqcst:
; CHECK:       SYNC 3
; CHECK:       CSWAP
; CHECK:       SYNC 3
; CHECK-NEXT:  POP 0, 0
define i1 @cmp_seqcst(ptr %p, i64 %expected, i64 %new) nounwind {
  %pair = cmpxchg ptr %p, i64 %expected, i64 %new seq_cst seq_cst
  %ok = extractvalue { i64, i1 } %pair, 1
  ret i1 %ok
}

; Atomic RMW operations expand to a retrying cmpxchg loop.
; CHECK-LABEL: rmw_add:
; CHECK:       SYNC 3
; CHECK:       LDOU
; CHECK:       ADDU
; CHECK:       CSWAP
; CHECK:       BNZB
; CHECK:       SYNC 3
define i64 @rmw_add(ptr %p, i64 %value) {
  %old = atomicrmw add ptr %p, i64 %value acq_rel
  ret i64 %old
}

; Sub-octabyte operations use a big-endian masked octabyte cmpxchg loop.
; CHECK-LABEL: rmw_add_i8:
; CHECK:       ANDN
; CHECK:       LDOU
; CHECK:       AND
; CHECK:       CSWAP
; CHECK:       BNZB
define i8 @rmw_add_i8(ptr %p, i8 %value) {
  %old = atomicrmw add ptr %p, i8 %value monotonic
  ret i8 %old
}

; Naturally aligned i16 and i32 RMW operations use the same big-endian masked
; octabyte loop. The mask is ordinary SSA/GPR state, not architectural rM.
; CHECK-LABEL: rmw_xor_i16:
; CHECK-NOT:   PUT rM
; CHECK:       ANDN
; CHECK:       LDOU
; CHECK:       XOR
; CHECK:       CSWAP
; CHECK:       BNZB
; CHECK-NOT:   PUT rM
define i16 @rmw_xor_i16(ptr %p, i16 %value) {
  %old = atomicrmw xor ptr %p, i16 %value monotonic
  ret i16 %old
}

; CHECK-LABEL: rmw_or_i32:
; CHECK-NOT:   PUT rM
; CHECK:       ANDN
; CHECK:       LDOU
; CHECK:       OR
; CHECK:       CSWAP
; CHECK:       BNZB
; CHECK-NOT:   PUT rM
define i32 @rmw_or_i32(ptr %p, i32 %value) {
  %old = atomicrmw or ptr %p, i32 %value monotonic
  ret i32 %old
}

; The remaining integer RMW operations use the same retry contract. These
; checks identify the semantic operation without depending on the shape of
; AtomicExpand's IR control flow.
; CHECK-LABEL: rmw_sub:
; CHECK:       LDOU
; CHECK:       SUBU
; CHECK:       CSWAP
; CHECK:       BNZB
define i64 @rmw_sub(ptr %p, i64 %value) {
  %old = atomicrmw sub ptr %p, i64 %value monotonic
  ret i64 %old
}

; CHECK-LABEL: rmw_and_i8:
; CHECK:       LDOU
; CHECK:       AND
; CHECK:       OR
; CHECK:       CSWAP
; CHECK:       BNZB
define i8 @rmw_and_i8(ptr %p, i8 %value) {
  %old = atomicrmw and ptr %p, i8 %value monotonic
  ret i8 %old
}

; CHECK-LABEL: rmw_nand_i16:
; CHECK:       LDOU
; CHECK:       AND
; CHECK:       ANDN
; CHECK:       OR
; CHECK:       CSWAP
; CHECK:       BNZB
define i16 @rmw_nand_i16(ptr %p, i16 %value) {
  %old = atomicrmw nand ptr %p, i16 %value monotonic
  ret i16 %old
}

; Signed narrow extrema sign-extend the selected field before comparison.
; CHECK-LABEL: rmw_min_i16:
; CHECK:       LDOU
; CHECK:       SRU
; CHECK:       SLU
; CHECK:       SR
; CHECK:       CMP
; CHECK:       CSWAP
; CHECK:       BNZB
define i16 @rmw_min_i16(ptr %p, i16 %value) {
  %old = atomicrmw min ptr %p, i16 %value monotonic
  ret i16 %old
}

; CHECK-LABEL: rmw_max_i32:
; CHECK:       LDOU
; CHECK:       SRU
; CHECK:       SLU
; CHECK:       SR
; CHECK:       CMP
; CHECK:       CSWAP
; CHECK:       BNZB
define i32 @rmw_max_i32(ptr %p, i32 %value) {
  %old = atomicrmw max ptr %p, i32 %value monotonic
  ret i32 %old
}

; Unsigned extrema mask the selected field and use an unsigned comparison.
; CHECK-LABEL: rmw_umin_i8:
; CHECK:       LDOU
; CHECK:       SRU
; CHECK:       AND
; CHECK:       CMPU
; CHECK:       CSWAP
; CHECK:       BNZB
define i8 @rmw_umin_i8(ptr %p, i8 %value) {
  %old = atomicrmw umin ptr %p, i8 %value monotonic
  ret i8 %old
}

; CHECK-LABEL: rmw_umax_i32:
; CHECK:       LDOU
; CHECK:       SRU
; CHECK:       AND
; CHECK:       CMPU
; CHECK:       CSWAP
; CHECK:       BNZB
define i32 @rmw_umax_i32(ptr %p, i32 %value) {
  %old = atomicrmw umax ptr %p, i32 %value monotonic
  ret i32 %old
}

; Narrow cmpxchg uses an aligned octabyte CSWAP while preserving the selected
; big-endian subfield and returning the narrow old value and success flag.
; CHECK-LABEL: cmp_i8:
; CHECK-NOT:   PUT rM
; CHECK:       PUT rP
; CHECK:       CSWAP
; CHECK:       GET {{.*}}, rP
; CHECK-NOT:   PUT rM
define i1 @cmp_i8(ptr %p, i8 %expected, i8 %new) {
  %pair = cmpxchg ptr %p, i8 %expected, i8 %new monotonic monotonic
  %ok = extractvalue { i8, i1 } %pair, 1
  ret i1 %ok
}

; CHECK-LABEL: cmp_i16:
; CHECK-NOT:   PUT rM
; CHECK:       PUT rP
; CHECK:       CSWAP
; CHECK:       GET {{.*}}, rP
; CHECK-NOT:   PUT rM
define i16 @cmp_i16(ptr %p, i16 %expected, i16 %new) {
  %pair = cmpxchg ptr %p, i16 %expected, i16 %new monotonic monotonic
  %old = extractvalue { i16, i1 } %pair, 0
  ret i16 %old
}

; CHECK-LABEL: cmp_i32:
; CHECK-NOT:   PUT rM
; CHECK:       ANDN
; CHECK:       LDOU
; CHECK:       ANDN
; CHECK:       PUT rP
; CHECK:       CSWAP
; CHECK:       GET {{.*}}, rP
; CHECK:       AND
; CHECK:       BNZB
; CHECK-NOT:   PUT rM
define i32 @cmp_i32(ptr %p, i32 %expected, i32 %new) {
  %pair = cmpxchg ptr %p, i32 %expected, i32 %new monotonic monotonic
  %old = extractvalue { i32, i1 } %pair, 0
  ret i32 %old
}

; Atomic loads are read-only; acquire ordering is a trailing memory fence.
; CHECK-LABEL: load_acquire:
; CHECK:       LDOU r231, r231, 0
; CHECK-NEXT:  SYNC 3
define i64 @load_acquire(ptr %p) {
  %value = load atomic i64, ptr %p acquire, align 8
  ret i64 %value
}

; Sequentially consistent loads use the contracted system fence on both sides.
; CHECK-LABEL: load_seq_cst:
; CHECK:       SYNC 3
; CHECK:       LDOU r231, r231, 0
; CHECK-NEXT:  SYNC 3
define i64 @load_seq_cst(ptr %p) {
  %value = load atomic i64, ptr %p seq_cst, align 8
  ret i64 %value
}

; CHECK-LABEL: load_monotonic_i8:
; CHECK-NOT:   PUT rM
; CHECK:       LDBU r231, r231, 0
; CHECK-NOT:   PUT rM
define i8 @load_monotonic_i8(ptr %p) {
  %value = load atomic i8, ptr %p monotonic, align 1
  ret i8 %value
}

; CHECK-LABEL: load_monotonic_i16:
; CHECK-NOT:   PUT rM
; CHECK:       LDWU r231, r231, 0
; CHECK-NOT:   PUT rM
define i16 @load_monotonic_i16(ptr %p) {
  %value = load atomic i16, ptr %p monotonic, align 2
  ret i16 %value
}

; CHECK-LABEL: load_acquire_i32:
; CHECK-NOT:   PUT rM
; CHECK:       LDTU r231, r231, 0
; CHECK:       SYNC 3
; CHECK-NOT:   PUT rM
define i32 @load_acquire_i32(ptr %p) {
  %value = load atomic i32, ptr %p acquire, align 4
  ret i32 %value
}

; Atomic stores use a retrying exchange loop and release ordering is a leading
; memory fence.
; CHECK-LABEL: store_release:
; CHECK:       SYNC 3
; CHECK:       LDOU
; CHECK:       CSWAP
; CHECK:       BNZB
define void @store_release(ptr %p, i64 %value) {
  store atomic i64 %value, ptr %p release, align 8
  ret void
}

; CHECK-LABEL: store_release_i16:
; CHECK-NOT:   PUT rM
; CHECK:       SYNC 3
; CHECK:       LDOU
; CHECK:       CSWAP
; CHECK:       BNZB
; CHECK-NOT:   PUT rM
define void @store_release_i16(ptr %p, i16 %value) {
  store atomic i16 %value, ptr %p release, align 2
  ret void
}

; CHECK-LABEL: fences:
; CHECK:       SYNC 3
; CHECK-NEXT:  SYNC 3
; CHECK-NEXT:  SYNC 3
; CHECK-NEXT:  SYNC 3
; CHECK-NEXT:  POP 0, 0
define void @fences() nounwind {
  fence acquire
  fence release
  fence acq_rel
  fence seq_cst
  ret void
}

; A C signal fence is a compiler barrier and does not order MMIX hardware.
; CHECK-LABEL: signal_fence:
; CHECK-NOT:   SYNC
; CHECK:       POP 0, 0
define void @signal_fence() {
  fence syncscope("singlethread") seq_cst
  ret void
}
