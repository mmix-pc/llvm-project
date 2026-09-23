; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs \
; RUN:   -stop-after=mmix-isel %s -o - | FileCheck %s --check-prefix=ISEL
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=asm \
; RUN:   %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mmix-unknown-elf -verify-machineinstrs -filetype=obj \
; RUN:   %s -o %t.o
; RUN: llvm-readobj --relocations --expand-relocs %t.o \
; RUN:   | FileCheck %s --check-prefix=RELOCS --implicit-check-not=R_MMIX_
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=SYMBOLS

target triple = "mmix-unknown-elf"

; Library-semantic operations use their standard C helper names. Binary32
; operands and results remain raw low-tetra values in the ordinary ABI slots;
; binary64 values occupy one complete octa.

; ASM-LABEL: remainder_f32:
; ASM:       GETA {{r[0-9]+}}, %geta(fmodf)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ASM-NOT:   FREM
; ISEL-LABEL: name: remainder_f32
; ISEL:       DIRECT_CALL_STATE &fmodf, {{.*}}implicit $r231, implicit $r232, implicit-def $r254, implicit-def $r231
define float @remainder_f32(float %lhs, float %rhs) nounwind {
  %result = frem float %lhs, %rhs
  ret float %result
}

; ASM-LABEL: remainder_f64:
; ASM:       GETA {{r[0-9]+}}, %geta(fmod)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ASM-NOT:   FREM
; ISEL-LABEL: name: remainder_f64
; ISEL:       DIRECT_CALL_STATE &fmod, {{.*}}implicit $r231, implicit $r232, implicit-def $r254, implicit-def $r231
define double @remainder_f64(double %lhs, double %rhs) nounwind {
  %result = frem double %lhs, %rhs
  ret double %result
}

; ASM-LABEL: fused_multiply_add_f32:
; ASM:       GETA {{r[0-9]+}}, %geta(fmaf)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: fused_multiply_add_f32
; ISEL:       DIRECT_CALL_STATE &fmaf, {{.*}}implicit $r231, implicit $r232, implicit $r233, implicit-def $r254, implicit-def $r231
define float @fused_multiply_add_f32(float %lhs, float %rhs, float %addend) nounwind {
  %result = call float @llvm.fma.f32(float %lhs, float %rhs, float %addend)
  ret float %result
}

; ASM-LABEL: fused_multiply_add_f64:
; ASM:       GETA {{r[0-9]+}}, %geta(fma)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: fused_multiply_add_f64
; ISEL:       DIRECT_CALL_STATE &fma, {{.*}}implicit $r231, implicit $r232, implicit $r233, implicit-def $r254, implicit-def $r231
define double @fused_multiply_add_f64(double %lhs, double %rhs,
                                      double %addend) nounwind {
  %result = call double @llvm.fma.f64(double %lhs, double %rhs,
                                      double %addend)
  ret double %result
}

; ASM-LABEL: round_f32:
; ASM:       GETA {{r[0-9]+}}, %geta(roundf)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: round_f32
; ISEL:       DIRECT_CALL_STATE &roundf, {{.*}}implicit $r231, implicit-def $r254, implicit-def $r231
define float @round_f32(float %value) nounwind {
  %result = call float @llvm.round.f32(float %value)
  ret float %result
}

; ASM-LABEL: round_f64:
; ASM:       GETA {{r[0-9]+}}, %geta(round)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: round_f64
; ISEL:       DIRECT_CALL_STATE &round, {{.*}}implicit $r231, implicit-def $r254, implicit-def $r231
define double @round_f64(double %value) nounwind {
  %result = call double @llvm.round.f64(double %value)
  ret double %result
}

; ASM-LABEL: nearbyint_f32:
; ASM:       GETA {{r[0-9]+}}, %geta(nearbyintf)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: nearbyint_f32
; ISEL:       DIRECT_CALL_STATE &nearbyintf, {{.*}}implicit $r231, implicit-def $r254, implicit-def $r231
define float @nearbyint_f32(float %value) nounwind {
  %result = call float @llvm.nearbyint.f32(float %value)
  ret float %result
}

; ASM-LABEL: nearbyint_f64:
; ASM:       GETA {{r[0-9]+}}, %geta(nearbyint)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: nearbyint_f64
; ISEL:       DIRECT_CALL_STATE &nearbyint, {{.*}}implicit $r231, implicit-def $r254, implicit-def $r231
define double @nearbyint_f64(double %value) nounwind {
  %result = call double @llvm.nearbyint.f64(double %value)
  ret double %result
}

; An explicit long-double helper remains an ordinary symbol call. LLVM f64
; cannot distinguish C double from this target's representation-identical
; long double, so choosing the l suffix is a frontend responsibility.
; ASM-LABEL: explicit_fmal:
; ASM:       GETA {{r[0-9]+}}, %geta(fmal)
; ASM:       PUSHGO r31, {{r[0-9]+}}, 0
; ISEL-LABEL: name: explicit_fmal
; ISEL:       DIRECT_CALL_STATE @fmal, {{.*}}implicit $r231, implicit $r232, implicit $r233, implicit-def $r254, implicit-def $r231
define double @explicit_fmal(double %lhs, double %rhs, double %addend) nounwind {
  %result = call double @fmal(double %lhs, double %rhs, double %addend)
  ret double %result
}

; Every undefined helper remains an ordinary ELF symbol referenced through the
; GNU-compatible stubbable direct-call relocation.
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: fmodf
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: fmod
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: fmaf
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: fma
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: roundf
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: round
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: nearbyintf
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: nearbyint
; RELOCS:      Type: R_MMIX_GETA (13)
; RELOCS-NEXT: Symbol: fmal

; SYMBOLS:      Name: fmodf
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: fmod
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: fmaf
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: fma
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: roundf
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: round
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: nearbyintf
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: nearbyint
; SYMBOLS:      Section: Undefined
; SYMBOLS:      Name: fmal
; SYMBOLS:      Section: Undefined

declare float @llvm.fma.f32(float, float, float)
declare double @llvm.fma.f64(double, double, double)
declare float @llvm.round.f32(float)
declare double @llvm.round.f64(double)
declare float @llvm.nearbyint.f32(float)
declare double @llvm.nearbyint.f64(double)
declare double @fmal(double, double, double)
