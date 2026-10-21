//===-- strerror_r declaration selection -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_STRERROR_R_MACROS_H
#define LLVM_LIBC_MACROS_STRERROR_R_MACROS_H

#if !defined(_GNU_SOURCE) &&                                                   \
    ((defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L) ||               \
     (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 600) ||                       \
     defined(_DEFAULT_SOURCE) ||                                               \
     (!defined(__STRICT_ANSI__) && !defined(_POSIX_SOURCE) &&                  \
      !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)))
#define __LIBC_USE_POSIX_STRERROR_R 1
#endif

#endif // LLVM_LIBC_MACROS_STRERROR_R_MACROS_H
