//===-- MMIX Linux interpreter entry --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

  .section .text._start,"ax",@progbits
  .p2align 2
  .globl _start
  .hidden _start
  .type _start,@function
  .hidden __llvm_libc_loader_bootstrap
_start:
.Lentry:
  // r0 survives the bootstrap call in the root register window. Never return
  // through the incoming rJ; the kernel entered with no caller.
  OR r0,r254,0
  OR r231,r254,0
  GETAB r232,.Lentry
  PUSHJ r31,__llvm_libc_loader_bootstrap
  // Bootstrap returns the validated main entry after all of its windows have
  // unwound. The freestanding profile leaves TP and rG unchanged; it does not
  // install TLS or claim shared-libc adoption.
  OR r254,r0,0
  PUT rL,0
  GO r255,r231,0
  TRAP 255,0,0
  .size _start,.-_start
  .section .note.GNU-stack,"",@progbits
