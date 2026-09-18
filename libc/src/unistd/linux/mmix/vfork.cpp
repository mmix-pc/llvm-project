//===-- MMIX Linux vfork syscall entry ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/vfork.h"
#include "hdr/signal_macros.h"
#include "src/__support/macros/macro-utils.h"
#include <linux/sched.h>
#include <sys/syscall.h>

#if !defined(__mmix__) || !defined(__linux__)
#error "This vfork entry requires MMIX Linux"
#endif

// No software frame, saved rJ slot or helper call may span the clone trap.
// Errors create no child, so only that path may enter ordinary C++ code.
// FIXME: The current kernel rejects shared-VM clone. Qualify register-stack
// backing, signal and thread-state ownership before claiming successful vfork.
asm(".section .text.__llvm_libc_mmix_vfork,\"ax\",@progbits\n"
    ".p2align 2\n"
    ".global __llvm_libc_mmix_vfork\n"
    ".hidden __llvm_libc_mmix_vfork\n"
    ".type __llvm_libc_mmix_vfork,@function\n"
#ifdef LIBC_COPT_PUBLIC_PACKAGING
    ".global vfork\n"
    ".type vfork,@function\n"
    "vfork:\n"
#endif
    "__llvm_libc_mmix_vfork:\n"
    "SETL r231, " LLVM_LIBC_STRINGIFY(CLONE_VM | CLONE_VFORK | SIGCHLD) "\n"
    "SETL r232, 0\n"
    "SETL r233, 0\n"
    "SETL r234, 0\n"
    "SETL r235, 0\n"
    "SETL r236, 0\n"
    "SETL r237, " LLVM_LIBC_STRINGIFY(SYS_clone) "\n"
    "TRAP 1, 0, 0\n"
    "BNN r231, 1f\n"
    "JMP __llvm_libc_mmix_vfork_error\n"
    "1: POP 0, 0\n"
    ".size __llvm_libc_mmix_vfork,.-__llvm_libc_mmix_vfork\n"
#ifdef LIBC_COPT_PUBLIC_PACKAGING
    ".size vfork,.-vfork\n"
#endif
);
