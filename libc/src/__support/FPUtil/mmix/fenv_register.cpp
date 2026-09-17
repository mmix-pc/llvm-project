//===-- MMIX arithmetic status writer -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The GNU ABI passes the status value in the first global argument register.
asm(".text\n"
    ".p2align 2\n"
    ".globl __llvm_libc_mmix_set_fenv\n"
    ".hidden __llvm_libc_mmix_set_fenv\n"
    ".type __llvm_libc_mmix_set_fenv,@function\n"
    "__llvm_libc_mmix_set_fenv:\n"
    "PUT rA,r231\n"
    "POP 0,0\n"
    ".size __llvm_libc_mmix_set_fenv,.-__llvm_libc_mmix_set_fenv\n");
