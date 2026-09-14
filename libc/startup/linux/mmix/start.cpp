//===-- MMIX Linux process entry -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// FIXME: Replace TLS-free single-thread startup when MMIX Linux gains
// pthread/TLS and dynamic-loader integration.

// Capture the kernel's octa-aligned stack before any compiler prologue. Linux
// supplies rG, TP and the register-stack backing; PUSHJ creates the first call
// window without using an incoming rJ. The bootstrap takes the original SP as
// its sole argument and must not return.
asm(R"(
  .section .text,"ax",@progbits
  .p2align 2
  .globl _start
  .type _start,@function
  .hidden __llvm_libc_mmix_linux_start
_start:
  OR r231,r254,0
  PUSHJ r31,__llvm_libc_mmix_linux_start
.Lunexpected_return:
  TRAP 255,0,0
  JMPB .Lunexpected_return
  .size _start,.-_start
  .section .note.GNU-stack,"",@progbits
)");
