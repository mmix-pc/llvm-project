//===-- MMIX Linux nonreturning TLS handoff --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The ordinary C ABI supplies TP in r231 and prepared state in r232. Keep
// this transition outside LLVM IR: TLS addresses may be hoisted within a
// compiled activation. Never POP into the pre-TP caller, even on failure.
asm(R"(
  .section .text,"ax",@progbits
  .p2align 2
  .globl __llvm_libc_mmix_linux_tls_handoff
  .hidden __llvm_libc_mmix_linux_tls_handoff
  .type __llvm_libc_mmix_linux_tls_handoff,@function
  .hidden __llvm_libc_mmix_linux_post_tls
__llvm_libc_mmix_linux_tls_handoff:
  OR r230,r231,0
  OR r231,r232,0
  PUSHJ r31,__llvm_libc_mmix_linux_post_tls
.Ltls_unexpected_return:
  TRAP 255,0,0
  JMPB .Ltls_unexpected_return
  .size __llvm_libc_mmix_linux_tls_handoff,.-__llvm_libc_mmix_linux_tls_handoff
  .section .note.GNU-stack,"",@progbits
)");
