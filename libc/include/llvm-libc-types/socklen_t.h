//===-- Definition of socklen_t type ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_SOCKLEN_T_H
#define LLVM_LIBC_TYPES_SOCKLEN_T_H

#ifdef __linux__
// Linux socket syscalls read and write length pointers as 32-bit integers,
// including on LP64 targets.
typedef unsigned int socklen_t;
#else
typedef unsigned long socklen_t;
#endif

#endif // LLVM_LIBC_TYPES_SOCKLEN_T_H
