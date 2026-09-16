//===-- MMIX Linux two-entry signal trampoline ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The signal trampoline requires MMIX Linux"
#endif

#include <asm/unistd.h>

.section .text.__llvm_libc_mmix_signal_trampoline,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_signal_trampoline
.hidden __llvm_libc_mmix_signal_trampoline
.type __llvm_libc_mmix_signal_trampoline,@function
__llvm_libc_mmix_signal_trampoline:
  // Preserve rJ, arguments, TP and SP=F. The kernel seeded the sole zero hole;
  // an ordinary handler POP consumes it without a trampoline PUSH.
  GO r255, r234, 0
.global __llvm_libc_mmix_signal_return
.hidden __llvm_libc_mmix_signal_return
.type __llvm_libc_mmix_signal_return,@function
__llvm_libc_mmix_signal_return:
  // The handler's ABI epilogue restores SP=F, the kernel's exact frame locator.
  // Do not derive F from caller-clobbered arguments or perform another POP.
  SETL r237, __NR_rt_sigreturn
  TRAP 1, 0, 0
  // Success never returns. Any return is terminal, without stack access.
  .hidden __llvm_libc_mmix_jump_fail
  JMP __llvm_libc_mmix_jump_fail
.size __llvm_libc_mmix_signal_return, .-__llvm_libc_mmix_signal_return
.size __llvm_libc_mmix_signal_trampoline, .-__llvm_libc_mmix_signal_trampoline
.section .note.GNU-stack,"",@progbits
