//===-- Linux setjmp for MMIX --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "save.h"
#include "src/__support/common.h"
#include "src/setjmp/setjmp_impl.h"

// The internal declaration names this assembly entry directly, without a
// C++ wrapper window. Public packaging adds a second label at the same entry.
asm(".section .text.__llvm_libc_mmix_linux_setjmp,\"ax\",@progbits\n"
    ".p2align 2\n"
    ".global __llvm_libc_mmix_linux_setjmp\n"
    ".hidden __llvm_libc_mmix_linux_setjmp\n"
    ".type __llvm_libc_mmix_linux_setjmp,@function\n"
#ifdef LIBC_COPT_PUBLIC_PACKAGING
    ".global setjmp\n"
    ".type setjmp,@function\n"
    "setjmp:\n"
#endif
    "__llvm_libc_mmix_linux_setjmp:\n"
    "SETL r232, 0\n" MMIX_LINUX_SAVE_BODY
    ".size __llvm_libc_mmix_linux_setjmp,.-__llvm_libc_mmix_linux_setjmp\n"
#ifdef LIBC_COPT_PUBLIC_PACKAGING
    ".size setjmp,.-setjmp\n"
#endif
);
