//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <mmix/UnwindConfig.h>
#if defined(__linux__)
#include <asm/unistd.h>
#endif

.cfi_sections .debug_frame
.p2align 2
DEFINE_LIBUNWIND_FUNCTION(__libunwind_Registers_mmix_jumpto)
.cfi_startproc
.cfi_def_cfa 30, 0
.cfi_undefined 35
  AND r0, r231, 7
  BNZ r0, .Lmmix_restore_invalid
  BZ r231, .Lmmix_restore_invalid
  OR r251, r231, 0
  SETL r0, MMIX_UNWIND_RD_OFFSET
  ADDU r0, r251, r0
  LDOU r1, r0, (MMIX_UNWIND_IP_OFFSET - MMIX_UNWIND_RD_OFFSET)
  BZ r1, .Lmmix_restore_invalid
  AND r1, r1, 3
  BNZ r1, .Lmmix_restore_invalid
  LDOU r1, r0, (MMIX_UNWIND_RG_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r2, rG
  CMPU r2, r1, r2
  BNZ r2, .Lmmix_restore_invalid
#if defined(__linux__)
  CMPU r2, r1, 230
  BNZ r2, .Lmmix_restore_invalid
#else
  CMPU r2, r1, 231
  BP r2, .Lmmix_restore_invalid
#endif
  LDOU r2, r0, (MMIX_UNWIND_RL_OFFSET - MMIX_UNWIND_RD_OFFSET)
  CMPU r2, r2, r1
  BP r2, .Lmmix_restore_invalid
  LDOU r252, r0, (MMIX_UNWIND_RO_OFFSET - MMIX_UNWIND_RD_OFFSET)
  AND r1, r252, 7
  BNZ r1, .Lmmix_restore_invalid
  SUBU r1, r0, (MMIX_UNWIND_RD_OFFSET - MMIX_UNWIND_GPR_OFFSET(254))
  LDOU r1, r1, 0
  AND r1, r1, 7
  BNZ r1, .Lmmix_restore_invalid

  // A failed or non-progressing ancestor search must never install a context.
  GETA r255, .Lmmix_restore_after_pop
  PUT rJ, r255
.Lmmix_restore_check:
  GET r250, rO
  CMPU r250, r250, r252
  BZ r250, .Lmmix_restore_window
  BN r250, .Lmmix_restore_invalid
  GET r255, rO
  POP 0, 0
.Lmmix_restore_after_pop:
  GET r250, rO
  CMPU r250, r250, r255
  BNN r250, .Lmmix_restore_invalid
  JMPB .Lmmix_restore_check

.Lmmix_restore_window:
  OR r255, r251, 0
  .irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    LDOU r\reg, r255, ((\reg - 0) * 8)
  .endr
  INCL r255, 256
  .irp reg, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
    LDOU r\reg, r255, ((\reg - 32) * 8)
  .endr
  INCL r255, 256
  .irp reg, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95
    LDOU r\reg, r255, ((\reg - 64) * 8)
  .endr
  INCL r255, 256
  .irp reg, 96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
    LDOU r\reg, r255, ((\reg - 96) * 8)
  .endr
  INCL r255, 256
  .irp reg, 128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159
    LDOU r\reg, r255, ((\reg - 128) * 8)
  .endr
  INCL r255, 256
  .irp reg, 160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191
    LDOU r\reg, r255, ((\reg - 160) * 8)
  .endr
  INCL r255, 256
  .irp reg, 192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223
    LDOU r\reg, r255, ((\reg - 192) * 8)
  .endr
  INCL r255, 256
  .irp reg, 224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,251,252,253,254
#if defined(__linux__)
    // Preserve the current thread pointer, not the captured thread identity.
    .if \reg != 230
#endif
    LDOU r\reg, r255, ((\reg - 224) * 8)
#if defined(__linux__)
    .endif
#endif
  .endr
  INCL r255, 256
  LDOU r250, r255, (MMIX_UNWIND_RD_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rD, r250
  LDOU r250, r255, (MMIX_UNWIND_RE_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rE, r250
  LDOU r250, r255, (MMIX_UNWIND_RH_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rH, r250
  LDOU r250, r255, (MMIX_UNWIND_RJ_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rJ, r250
  LDOU r250, r255, (MMIX_UNWIND_RR_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rR, r250
  // rO/rS reflect architectural POP and spill/fill, not saved bit patterns.
  LDOU r250, r255, (MMIX_UNWIND_RL_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rL, r250
  LDOU r250, r255, (MMIX_UNWIND_IP_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GO r255, r250, 0
.Lmmix_restore_invalid:
#if defined(__linux__)
  // A partial POP search cannot safely call C, raise a signal, or use the stack.
  SETL r231, 127
  SETL r232, 0
  SETL r233, 0
  SETL r234, 0
  SETL r235, 0
  SETL r236, 0
  SETL r237, __NR_exit_group
  TRAP 1, 0, 0
.Lmmix_restore_exit_returned:
  JMP .Lmmix_restore_exit_returned
#else
  TRAP 255, 0, 0
  JMPB .Lmmix_restore_invalid
#endif
.cfi_endproc
.size __libunwind_Registers_mmix_jumpto, .-__libunwind_Registers_mmix_jumpto
