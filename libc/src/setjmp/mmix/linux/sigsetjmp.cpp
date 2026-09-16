//===-- Linux sigsetjmp for MMIX -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/setjmp/sigsetjmp.h"
#include "save.h"
#include "src/__support/common.h"

asm(".section .text.__llvm_libc_mmix_linux_sigsetjmp,\"ax\",@progbits\n"
    ".p2align 2\n"
    ".global __llvm_libc_mmix_linux_sigsetjmp\n"
    ".hidden __llvm_libc_mmix_linux_sigsetjmp\n"
    ".type __llvm_libc_mmix_linux_sigsetjmp,@function\n"
#ifdef LIBC_COPT_PUBLIC_PACKAGING
    ".global sigsetjmp\n"
    ".type sigsetjmp,@function\n"
    "sigsetjmp:\n"
#endif
    "__llvm_libc_mmix_linux_sigsetjmp:\n" MMIX_LINUX_SAVE_BODY
    ".size "
    "__llvm_libc_mmix_linux_sigsetjmp,.-__llvm_libc_mmix_linux_sigsetjmp\n"
#ifdef LIBC_COPT_PUBLIC_PACKAGING
    ".size sigsetjmp,.-sigsetjmp\n"
#endif
);
