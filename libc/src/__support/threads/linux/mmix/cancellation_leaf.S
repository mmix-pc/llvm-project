//===-- MMIX Linux resource-free cancellation leaf ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

.section .text.__llvm_libc_mmix_cancel_leaf,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_cancel_leaf
.hidden __llvm_libc_mmix_cancel_leaf
.type __llvm_libc_mmix_cancel_leaf,@function
__llvm_libc_mmix_cancel_leaf:
  // Arguments: number, six syscall words, private descriptor in r238.
  // Prepare expected leaf identity before the recoverable interval. No call,
  // window growth, SP adjustment or store occurs inside that interval.
  GET r255, rO
  STOU r255, r238, 0
  STOU r254, r238, 8
  LDOU r250, r238, 16
  OR r255, r231, 0
  OR r231, r232, 0
  OR r232, r233, 0
  OR r233, r234, 0
  OR r234, r235, 0
  OR r235, r236, 0
  OR r236, r237, 0
  OR r237, r255, 0
.global __llvm_libc_mmix_cancel_entry
.hidden __llvm_libc_mmix_cancel_entry
__llvm_libc_mmix_cancel_entry:
  // Single-CPU preparation: an aligned atomic octa load observes sticky state.
  // A wake delivered before entry is caught here, closing check-to-block.
  LDOU r255, r250, 0
  AND r255, r255, 7
  CMPU r255, r255, 3
  BZ r255, .Lpending
  TRAP 1, 0, 0
.global __llvm_libc_mmix_cancel_after
.hidden __llvm_libc_mmix_cancel_after
__llvm_libc_mmix_cancel_after:
  POP 0, 0
.Lpending:
  // Private no-operation result, distinct from every Linux errno.
  SETL r231, 4096
  NEGU r231, 0, r231
  POP 0, 0
.size __llvm_libc_mmix_cancel_leaf, .-__llvm_libc_mmix_cancel_leaf
.section .note.GNU-stack,"",@progbits
