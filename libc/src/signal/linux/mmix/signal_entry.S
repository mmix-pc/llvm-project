//===-- MMIX application signal boundary entry -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The application signal entry requires MMIX Linux"
#endif

.section .text.__llvm_libc_mmix_application_signal_trampoline,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_application_signal_trampoline
.hidden __llvm_libc_mmix_application_signal_trampoline
.type __llvm_libc_mmix_application_signal_trampoline,@function
__llvm_libc_mmix_application_signal_trampoline:
  JMP .Lapplication_signal_entry
  // Preserve the kernel's restorer+4 return convention and exact SP=F.
  .hidden __llvm_libc_mmix_signal_return
  JMP __llvm_libc_mmix_signal_return
.Lapplication_signal_entry:
  // Four kernel arguments remain intact; argument five is the trusted upper
  // bound of the wrapper frame. Do not PUSH another synthetic call window.
  OR r235, r254, 0
  .hidden __llvm_libc_mmix_application_signal_entry
  JMP __llvm_libc_mmix_application_signal_entry
.size __llvm_libc_mmix_application_signal_trampoline, .-__llvm_libc_mmix_application_signal_trampoline

.section .text.__llvm_libc_mmix_invoke_signal_handler,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_invoke_signal_handler
.hidden __llvm_libc_mmix_invoke_signal_handler
.type __llvm_libc_mmix_invoke_signal_handler,@function
__llvm_libc_mmix_invoke_signal_handler:
  // The wrapper made the ordinary call window. Tail dispatch accepts either
  // handler ABI without calling through an incompatible C++ function type.
  GO r255, r234, 0
.size __llvm_libc_mmix_invoke_signal_handler, .-__llvm_libc_mmix_invoke_signal_handler
.section .note.GNU-stack,"",@progbits
