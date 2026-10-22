//===-- MMIX Linux public namespace selection ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_MMIX_LINUX_FEATURES_H
#define LLVM_LIBC_MACROS_MMIX_LINUX_FEATURES_H

// These are namespace requests, not runtime capability or conformance claims.
#define __LIBC_EXPLICIT_NAMESPACE 1
#if defined(_GNU_SOURCE) ||                                                 \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#define __LIBC_USE_C23_EXTENSIONS 1
#endif
#if defined(_GNU_SOURCE)
#define __LIBC_USE_GNU 1
#define __LIBC_USE_DEFAULT 1
#define __LIBC_POSIX_LEVEL 202405L
#define __LIBC_XOPEN_LEVEL 800
#else
#if defined(_DEFAULT_SOURCE) ||                                              \
    (!defined(__STRICT_ANSI__) && !defined(_POSIX_SOURCE) &&                  \
     !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))
#define __LIBC_USE_DEFAULT 1
#endif

#if defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 800
#define __LIBC_XOPEN_LEVEL 800
#elif defined(_XOPEN_SOURCE)
#define __LIBC_XOPEN_LEVEL _XOPEN_SOURCE
#else
#define __LIBC_XOPEN_LEVEL 0
#endif

#if defined(__LIBC_USE_DEFAULT) ||                                           \
    (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 202405L) ||               \
    __LIBC_XOPEN_LEVEL >= 800
#define __LIBC_POSIX_LEVEL 202405L
#elif (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L) ||             \
    __LIBC_XOPEN_LEVEL >= 700
#define __LIBC_POSIX_LEVEL 200809L
#elif (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L) ||             \
    __LIBC_XOPEN_LEVEL >= 600
#define __LIBC_POSIX_LEVEL 200112L
#elif __LIBC_XOPEN_LEVEL >= 500 &&                                          \
    (!defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 199506L)
#define __LIBC_POSIX_LEVEL 199506L
#elif defined(_POSIX_C_SOURCE)
#define __LIBC_POSIX_LEVEL _POSIX_C_SOURCE
#elif defined(_POSIX_SOURCE) || defined(_XOPEN_SOURCE)
#define __LIBC_POSIX_LEVEL 1
#else
#define __LIBC_POSIX_LEVEL 0
#endif
#endif

#if __LIBC_POSIX_LEVEL > 0
#define __LIBC_USE_POSIX 1
#endif
#if __LIBC_POSIX_LEVEL >= 199309L
#define __LIBC_USE_POSIX1993 1
#endif
#if __LIBC_POSIX_LEVEL >= 199506L
#define __LIBC_USE_POSIX1995 1
#endif
#if __LIBC_POSIX_LEVEL >= 200112L
#define __LIBC_USE_POSIX2001 1
#endif
#if __LIBC_POSIX_LEVEL >= 200809L
#define __LIBC_USE_POSIX2008 1
#endif
#if __LIBC_POSIX_LEVEL >= 202405L
#define __LIBC_USE_POSIX2024 1
#endif
#if __LIBC_XOPEN_LEVEL > 0
#define __LIBC_USE_XOPEN 1
#endif
#if __LIBC_XOPEN_LEVEL >= 700
#define __LIBC_USE_XSI7 1
#endif
#if __LIBC_XOPEN_LEVEL >= 800
#define __LIBC_USE_XSI8 1
#endif

#endif // LLVM_LIBC_MACROS_MMIX_LINUX_FEATURES_H
