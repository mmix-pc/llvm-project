//===-- Definition of macros from locale.h --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_LOCALE_MACROS_H
#define LLVM_LIBC_MACROS_LOCALE_MACROS_H

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX2008)
#include "../llvm-libc-types/locale_t.h"
#endif

#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define __LLVM_LIBC_LC_MESSAGES 5
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX)
#define LC_MESSAGES __LLVM_LIBC_LC_MESSAGES
#endif
#define LC_ALL 6

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX2008)
#define LC_GLOBAL_LOCALE ((locale_t)(-1))

#define LC_CTYPE_MASK (1 << LC_CTYPE)
#define LC_NUMERIC_MASK (1 << LC_NUMERIC)
#define LC_TIME_MASK (1 << LC_TIME)
#define LC_COLLATE_MASK (1 << LC_COLLATE)
#define LC_MONETARY_MASK (1 << LC_MONETARY)
#define LC_MESSAGES_MASK (1 << LC_MESSAGES)
#define LC_ALL_MASK 0x7fffffff
#endif

#endif // LLVM_LIBC_MACROS_LOCALE_MACROS_H
