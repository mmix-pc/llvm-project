//===-- C thread macros ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_THREADS_MACROS_H
#define LLVM_LIBC_MACROS_THREADS_MACROS_H

#if !defined(__cplusplus) &&                                             \
    (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define thread_local _Thread_local
#endif

#define TSS_DTOR_ITERATIONS 4

#endif // LLVM_LIBC_MACROS_THREADS_MACROS_H
