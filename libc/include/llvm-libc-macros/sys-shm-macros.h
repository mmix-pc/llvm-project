//===-- sys-shm-macros definitions -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_SYS_SHM_MACROS_H
#define LLVM_LIBC_MACROS_SYS_SHM_MACROS_H

#if defined(__linux__)
#define SHM_RDONLY 010000
#define SHM_RND 020000
#endif

#if defined(__linux__) && defined(__mmix__)
// MMIX Linux selects an 8 KiB page size and asm-generic SHMLBA == PAGE_SIZE.
#define SHMLBA 8192
#endif

#define SHM_FAILED ((void *)-1)

#endif // LLVM_LIBC_MACROS_SYS_SHM_MACROS_H
