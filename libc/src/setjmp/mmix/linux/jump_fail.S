//===-- MMIX Linux terminal nonlocal-transfer failure --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if !defined(__mmix__) || !defined(__linux__)
#error "The nonlocal-transfer failure path requires MMIX Linux"
#endif

#include <asm/unistd.h>

// Enter by JMP, without another call window or software-stack access.
// Syscall filtering must permit exit_group; if it returns, never resume the
// failed transfer or invoke another signal-dependent termination mechanism.
.section .text.__llvm_libc_mmix_jump_fail,"ax",@progbits
.p2align 2
.global __llvm_libc_mmix_jump_fail
.hidden __llvm_libc_mmix_jump_fail
.type __llvm_libc_mmix_jump_fail,@function
__llvm_libc_mmix_jump_fail:
  SETL r231, 127
  SETL r232, 0
  SETL r233, 0
  SETL r234, 0
  SETL r235, 0
  SETL r236, 0
  SETL r237, __NR_exit_group
  TRAP 1, 0, 0
1:
  JMP 1b
.size __llvm_libc_mmix_jump_fail, .-__llvm_libc_mmix_jump_fail

.section .note.GNU-stack,"",@progbits
