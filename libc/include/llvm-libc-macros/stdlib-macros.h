//===-- Definition of macros to be used with stdlib functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_STDLIB_MACROS_H
#define LLVM_LIBC_MACROS_STDLIB_MACROS_H

#include "../llvm-libc-types/size_t.h"

#ifdef _GNU_SOURCE
#include "alloca-macros.h"
#endif

#ifndef NULL
#define __need_NULL
#include <stddef.h>
#endif // NULL

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifndef MB_CUR_MAX
#if defined(__cplusplus) && __cplusplus >= 201103L
extern "C" size_t __ctype_get_mb_cur_max(void) noexcept;
#elif defined(__cplusplus)
extern "C" size_t __ctype_get_mb_cur_max(void) throw();
#else
size_t __ctype_get_mb_cur_max(void);
#endif
#define MB_CUR_MAX (__ctype_get_mb_cur_max())
#endif // MB_CUR_MAX

#define RAND_MAX 2147483647

#endif // LLVM_LIBC_MACROS_STDLIB_MACROS_H
