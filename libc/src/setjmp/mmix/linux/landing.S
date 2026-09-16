//===-- MMIX Linux prepared jump landing ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The prepared jump landing requires MMIX Linux"
#endif

// The kernel installs a trusted POP-traversable chain and copied arguments.
// No old software-stack or request access is valid after preparation.
.section .text.__llvm_libc_mmix_jump_landing,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_jump_landing
.hidden __llvm_libc_mmix_jump_landing
.type __llvm_libc_mmix_jump_landing,@function
__llvm_libc_mmix_jump_landing:
  OR r251, r231, 0
  LDOU r252, r251, 24
  AND r255, r252, 7
  BNZ r255, .Linvalid
.Lcheck:
  GET r255, rO
  CMPU r231, r255, r252
  BZ r231, .Ldone
  BN r231, .Linvalid
  GETA r231, .Lafter_pop
  PUT rJ, r231
  POP 0, 0
.Lafter_pop:
  GET r231, rO
  CMPU r231, r231, r255
  BNN r231, .Linvalid
  JMPB .Lcheck
.Ldone:
  ADDU r252, r251, 32
  .irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    LDOU r\reg, r252, (8 * \reg)
  .endr
  SETL r255, 31
  PUT rL, r255
  LDOU r253, r251, 0
  LDOU r254, r251, 16
  LDOU r255, r251, 8
  OR r231, r232, 0
  GO r237, r255, 0
.Linvalid:
  .hidden __llvm_libc_mmix_jump_fail
  JMP __llvm_libc_mmix_jump_fail
.size __llvm_libc_mmix_jump_landing, .-__llvm_libc_mmix_jump_landing
.section .note.GNU-stack,"",@progbits
