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

// Capture returns directly to its caller after removing its own window.
// These entrypoints are not synchronous unwind frames.
.cfi_sections .debug_frame
.p2align 2
DEFINE_LIBUNWIND_FUNCTION(__unw_getcontext)
.cfi_startproc
.cfi_def_cfa 30, 0
.cfi_same_value 35
.cfi_remember_state
  AND r0, r231, 7
  BNZ r0, .Lmmix_capture_invalid
  BZ r231, .Lmmix_capture_invalid
  GET r0, rG
#if defined(__linux__)
  CMPU r0, r0, 230
  BNZ r0, .Lmmix_capture_invalid
#else
  CMPU r0, r0, 231
  BP r0, .Lmmix_capture_invalid
#endif

  // Preserve global scratch values before using them after POP.
  SETL r0, MMIX_UNWIND_GPR_OFFSET(224)
  ADDU r0, r231, r0
#if defined(__linux__)
  STOU r237, r0, (MMIX_UNWIND_GPR_OFFSET(237) - MMIX_UNWIND_GPR_OFFSET(224))
#endif
  STOU r251, r0, (MMIX_UNWIND_GPR_OFFSET(251) - MMIX_UNWIND_GPR_OFFSET(224))
  STOU r252, r0, (MMIX_UNWIND_GPR_OFFSET(252) - MMIX_UNWIND_GPR_OFFSET(224))
  STOU r255, r0, (MMIX_UNWIND_GPR_OFFSET(255) - MMIX_UNWIND_GPR_OFFSET(224))
  INCL r0, 256
  GET r252, rJ
  STOU r252, r0, (MMIX_UNWIND_RJ_OFFSET - MMIX_UNWIND_RD_OFFSET)
  STOU r252, r0, (MMIX_UNWIND_IP_OFFSET - MMIX_UNWIND_RD_OFFSET)
.cfi_register 35, 28
  OR r251, r231, 0
#if defined(__linux__)
  // Materialize ancestors in kernel-owned backing without a user SAVE image.
  // Failure must return through the still-live capture window.
  SETL r237, __NR_mmix_rstack_sync
  TRAP 1, 0, 0
  BNZ r231, .Lmmix_capture_sync_error
#endif
  GETA r255, .Lmmix_capture_caller
  PUT rJ, r255
  POP 0, 0
.Lmmix_capture_caller:
#if !defined(__linux__)
  // Flush ancestor windows, then discard the transient SAVE image immediately.
  SAVE r255
  UNSAVE r255
#endif
  SETL r231, 0
  .irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    STOU r\reg, r251, ((\reg - 0) * 8)
  .endr
  INCL r251, 256
  .irp reg, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
    STOU r\reg, r251, ((\reg - 32) * 8)
  .endr
  INCL r251, 256
  .irp reg, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95
    STOU r\reg, r251, ((\reg - 64) * 8)
  .endr
  INCL r251, 256
  .irp reg, 96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
    STOU r\reg, r251, ((\reg - 96) * 8)
  .endr
  INCL r251, 256
  .irp reg, 128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159
    STOU r\reg, r251, ((\reg - 128) * 8)
  .endr
  INCL r251, 256
  .irp reg, 160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191
    STOU r\reg, r251, ((\reg - 160) * 8)
  .endr
  INCL r251, 256
  .irp reg, 192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223
    STOU r\reg, r251, ((\reg - 192) * 8)
  .endr
  INCL r251, 256
  .irp reg, 224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,253,254
#if defined(__linux__)
    .if \reg != 237
#endif
    STOU r\reg, r251, ((\reg - 224) * 8)
#if defined(__linux__)
    .endif
#endif
  .endr
  INCL r251, 256
  GET r255, rD
  STOU r255, r251, (MMIX_UNWIND_RD_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rE
  STOU r255, r251, (MMIX_UNWIND_RE_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rH
  STOU r255, r251, (MMIX_UNWIND_RH_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rR
  STOU r255, r251, (MMIX_UNWIND_RR_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rO
  STOU r255, r251, (MMIX_UNWIND_RO_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rS
  STOU r255, r251, (MMIX_UNWIND_RS_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rG
  STOU r255, r251, (MMIX_UNWIND_RG_OFFSET - MMIX_UNWIND_RD_OFFSET)
  GET r255, rL
  STOU r255, r251, (MMIX_UNWIND_RL_OFFSET - MMIX_UNWIND_RD_OFFSET)
  PUT rJ, r252
  GO r255, r252, 0
.cfi_restore_state
#if defined(__linux__)
.Lmmix_capture_sync_error:
  SETL r231, 6540 // UNW_EUNSPEC, not a Linux errno value.
  NEG r231, 0, r231
  POP 0, 0
#endif
.Lmmix_capture_invalid:
  SETL r231, 6547
  NEG r231, 0, r231
  POP 0, 0
.cfi_endproc
.size __unw_getcontext, .-__unw_getcontext
