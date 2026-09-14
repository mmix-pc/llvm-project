; RUN: llc -mtriple=mmix-unknown-elf -O2 -verify-machineinstrs \
; RUN:   -stop-after=prolog-epilog %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mmix-unknown-elf -O2 -verify-machineinstrs \
; RUN:   -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-linux -O2 -verify-machineinstrs \
; RUN:   -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM \
; RUN:   --implicit-check-not=r230 --implicit-check-not="PUT rG"

target triple = "mmix-unknown-elf"

@global_state = global i64 0, align 8

define internal fastcc i64 @fast_leaf(i64 %value) noinline {
  %result = add i64 %value, 17
  ret i64 %result
}

; Seven values remain live in hardware-preserved local registers across a
; FastCC direct call. No ordinary spill object or stack frame is introduced.
; MIR-LABEL: name: mixed_direct_live
; MIR:       stackSize: 0
; MIR:       stack: []
; MIR:       $r30 = frame-setup GET $rj
; MIR:       CALL_STATE @fast_leaf, csr_mmix{{.*}}implicit $r254, implicit $rg, implicit $rl, implicit $ro
; MIR:       $rj = frame-destroy PUT $r30
; ASM-LABEL: mixed_direct_live:
; ASM:       GET r30, rJ
; ASM:       OR [[DIRECT_LIVE:r[0-9]+]], r238, 0
; ASM-NOT:   STO
; ASM:       PUSHJB r31, fast_leaf
; ASM:       ADDU r231, {{r[0-9]+}}, [[DIRECT_LIVE]]
; ASM-NOT:   LDO
; ASM:       PUT rJ, r30
define i64 @mixed_direct_live(
    i64 %a0, i64 %a1, i64 %a2, i64 %a3,
    i64 %a4, i64 %a5, i64 %a6, i64 %a7) nounwind noinline {
  %call = call fastcc i64 @fast_leaf(i64 %a0)
  %s0 = add i64 %call, %a1
  %s1 = add i64 %s0, %a2
  %s2 = add i64 %s1, %a3
  %s3 = add i64 %s2, %a4
  %s4 = add i64 %s3, %a5
  %s5 = add i64 %s4, %a6
  %s6 = add i64 %s5, %a7
  ret i64 %s6
}

; A FastCC function uses the identical mask and local-register preservation
; around an indirect call. PUSHGO retains the fixed r31 procedure operand.
; MIR-LABEL: name: fast_indirect_live
; MIR:       stackSize: 0
; MIR:       stack: []
; MIR:       $r30 = frame-setup GET $rj
; MIR:       CALL_STATE killed $r250, csr_mmix{{.*}}implicit $r254, implicit $rg, implicit $rl, implicit $ro
; MIR:       $rj = frame-destroy PUT $r30
; ASM-LABEL: fast_indirect_live:
; ASM:       GET r30, rJ
; ASM:       OR [[INDIRECT_LIVE:r[0-9]+]], r239, 0
; ASM-NOT:   STO
; ASM:       PUSHGO r31, r250, 0
; ASM:       ADDU r231, {{r[0-9]+}}, [[INDIRECT_LIVE]]
; ASM-NOT:   LDO
; ASM:       PUT rJ, r30
define fastcc i64 @fast_indirect_live(
    ptr %callee, i64 %a0, i64 %a1, i64 %a2, i64 %a3,
    i64 %a4, i64 %a5, i64 %a6, i64 %a7) nounwind noinline {
  %call = call fastcc i64 %callee(i64 %a0)
  %s0 = add i64 %call, %a1
  %s1 = add i64 %s0, %a2
  %s2 = add i64 %s1, %a3
  %s3 = add i64 %s2, %a4
  %s4 = add i64 %s3, %a5
  %s5 = add i64 %s4, %a6
  %s6 = add i64 %s5, %a7
  ret i64 %s6
}

; A C-convention outer function retains its inputs across nested direct and
; indirect FastCC calls without weakening either call boundary.
; MIR-LABEL: name: mixed_nested_live
; MIR:       stackSize: 0
; MIR:       stack: []
; MIR:       CALL_STATE @fast_leaf, csr_mmix
; MIR:       DIRECT_CALL_STATE @fast_indirect_live, {{.*}}csr_mmix
; ASM-LABEL: mixed_nested_live:
; ASM:       PUSHJB r31, fast_leaf
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ASM:       PUT rJ, r30
define i64 @mixed_nested_live(
    ptr %callee, i64 %a0, i64 %a1, i64 %a2, i64 %a3,
    i64 %a4, i64 %a5, i64 %a6, i64 %a7) nounwind noinline {
  %first = call fastcc i64 @fast_leaf(i64 %a0)
  %second = call fastcc i64 @fast_indirect_live(
      ptr %callee, i64 %first, i64 %a1, i64 %a2, i64 %a3,
      i64 %a4, i64 %a5, i64 %a6, i64 %a7)
  %result = add i64 %second, %a0
  ret i64 %result
}

; A volatile global value and its address survive the call in local registers;
; the global load and store do not require a stack spill object.
; MIR-LABEL: name: mixed_global_live
; MIR:       stackSize: 0
; MIR:       stack: []
; MIR:       $r1 = LDOUI $r0, 0 :: (volatile dereferenceable load (s64) from @global_state)
; MIR:       CALL_STATE @fast_leaf, csr_mmix
; MIR:       STOUI $r231, killed $r0, 0 :: (volatile store (s64) into @global_state)
; ASM-LABEL: mixed_global_live:
; ASM:       LDOU [[GLOBAL_VALUE:r[0-9]+]], [[GLOBAL_ADDR:r[0-9]+]], 0
; ASM:       PUSHJB r31, fast_leaf
; ASM:       ADDU r231, r231, [[GLOBAL_VALUE]]
; ASM:       STOU r231, [[GLOBAL_ADDR]], 0
define i64 @mixed_global_live(i64 %value) nounwind noinline {
  %before = load volatile i64, ptr @global_state, align 8
  %call = call fastcc i64 @fast_leaf(i64 %value)
  %result = add i64 %call, %before
  store volatile i64 %result, ptr @global_state, align 8
  ret i64 %result
}

; Frame-pointer state follows the C convention: SP reserves only the reviewed
; r253 save slot, rJ uses r30, and the FastCC call keeps rG/rL/rO implicit.
; MIR-LABEL: name: mixed_frame_live
; MIR:       stackSize: 8
; MIR:       $r30 = frame-setup GET $rj
; MIR:       $r254 = frame-setup SUBUI $r254, 8
; MIR:       STOUI killed $r253, $r254, 0
; MIR:       $r253 = frame-setup ADDUI $r254, 8
; MIR:       CALL_STATE @fast_leaf, csr_mmix{{.*}}implicit $r254, implicit $rg, implicit $rl, implicit $ro
; MIR:       $r253 = LDOU $r253, killed $r255
; MIR:       $r254 = frame-destroy ADDUI $r254, 8
; MIR:       $rj = frame-destroy PUT $r30
; ASM-LABEL: mixed_frame_live:
; ASM:       GET r30, rJ
; ASM:       SUBU r254, r254, 8
; ASM:       STOU r253, r254, 0
; ASM:       PUSHJB r31, fast_leaf
; ASM:       LDOU r253, r253, {{r[0-9]+}}
; ASM:       ADDU r254, r254, 8
; ASM:       PUT rJ, r30
define i64 @mixed_frame_live(i64 %value, i64 %live) nounwind #0 {
  %call = call fastcc i64 @fast_leaf(i64 %value)
  %result = add i64 %call, %live
  ret i64 %result
}

attributes #0 = { noinline "frame-pointer"="all" }
