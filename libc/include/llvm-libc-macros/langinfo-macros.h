//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Definition of macros from langinfo.h.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_LANGINFO_MACROS_H
#define LLVM_LIBC_MACROS_LANGINFO_MACROS_H

#include "locale-macros.h"

#define _NL_ITEM(category, index) (((category) << 16) | (index))

#define CODESET _NL_ITEM(LC_CTYPE, 0)

#define RADIXCHAR _NL_ITEM(LC_NUMERIC, 0)
#define THOUSEP _NL_ITEM(LC_NUMERIC, 1)

#define D_T_FMT _NL_ITEM(LC_TIME, 0)
#define D_FMT _NL_ITEM(LC_TIME, 1)
#define T_FMT _NL_ITEM(LC_TIME, 2)
#define T_FMT_AMPM _NL_ITEM(LC_TIME, 3)
#define AM_STR _NL_ITEM(LC_TIME, 4)
#define PM_STR _NL_ITEM(LC_TIME, 5)

#define DAY_1 _NL_ITEM(LC_TIME, 6)
#define DAY_2 _NL_ITEM(LC_TIME, 7)
#define DAY_3 _NL_ITEM(LC_TIME, 8)
#define DAY_4 _NL_ITEM(LC_TIME, 9)
#define DAY_5 _NL_ITEM(LC_TIME, 10)
#define DAY_6 _NL_ITEM(LC_TIME, 11)
#define DAY_7 _NL_ITEM(LC_TIME, 12)

#define ABDAY_1 _NL_ITEM(LC_TIME, 13)
#define ABDAY_2 _NL_ITEM(LC_TIME, 14)
#define ABDAY_3 _NL_ITEM(LC_TIME, 15)
#define ABDAY_4 _NL_ITEM(LC_TIME, 16)
#define ABDAY_5 _NL_ITEM(LC_TIME, 17)
#define ABDAY_6 _NL_ITEM(LC_TIME, 18)
#define ABDAY_7 _NL_ITEM(LC_TIME, 19)

#define MON_1 _NL_ITEM(LC_TIME, 20)
#define MON_2 _NL_ITEM(LC_TIME, 21)
#define MON_3 _NL_ITEM(LC_TIME, 22)
#define MON_4 _NL_ITEM(LC_TIME, 23)
#define MON_5 _NL_ITEM(LC_TIME, 24)
#define MON_6 _NL_ITEM(LC_TIME, 25)
#define MON_7 _NL_ITEM(LC_TIME, 26)
#define MON_8 _NL_ITEM(LC_TIME, 27)
#define MON_9 _NL_ITEM(LC_TIME, 28)
#define MON_10 _NL_ITEM(LC_TIME, 29)
#define MON_11 _NL_ITEM(LC_TIME, 30)
#define MON_12 _NL_ITEM(LC_TIME, 31)

#define ABMON_1 _NL_ITEM(LC_TIME, 32)
#define ABMON_2 _NL_ITEM(LC_TIME, 33)
#define ABMON_3 _NL_ITEM(LC_TIME, 34)
#define ABMON_4 _NL_ITEM(LC_TIME, 35)
#define ABMON_5 _NL_ITEM(LC_TIME, 36)
#define ABMON_6 _NL_ITEM(LC_TIME, 37)
#define ABMON_7 _NL_ITEM(LC_TIME, 38)
#define ABMON_8 _NL_ITEM(LC_TIME, 39)
#define ABMON_9 _NL_ITEM(LC_TIME, 40)
#define ABMON_10 _NL_ITEM(LC_TIME, 41)
#define ABMON_11 _NL_ITEM(LC_TIME, 42)
#define ABMON_12 _NL_ITEM(LC_TIME, 43)

#define ERA _NL_ITEM(LC_TIME, 44)
#define ERA_D_FMT _NL_ITEM(LC_TIME, 45)
#define ERA_D_T_FMT _NL_ITEM(LC_TIME, 46)
#define ERA_T_FMT _NL_ITEM(LC_TIME, 47)
#define ALT_DIGITS _NL_ITEM(LC_TIME, 48)

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX2024)
// Stand-alone month forms have distinct query identities even in locales
// where their strings are identical to the contextual forms.
#define ALTMON_1 _NL_ITEM(LC_TIME, 49)
#define ALTMON_2 _NL_ITEM(LC_TIME, 50)
#define ALTMON_3 _NL_ITEM(LC_TIME, 51)
#define ALTMON_4 _NL_ITEM(LC_TIME, 52)
#define ALTMON_5 _NL_ITEM(LC_TIME, 53)
#define ALTMON_6 _NL_ITEM(LC_TIME, 54)
#define ALTMON_7 _NL_ITEM(LC_TIME, 55)
#define ALTMON_8 _NL_ITEM(LC_TIME, 56)
#define ALTMON_9 _NL_ITEM(LC_TIME, 57)
#define ALTMON_10 _NL_ITEM(LC_TIME, 58)
#define ALTMON_11 _NL_ITEM(LC_TIME, 59)
#define ALTMON_12 _NL_ITEM(LC_TIME, 60)
#define ABALTMON_1 _NL_ITEM(LC_TIME, 61)
#define ABALTMON_2 _NL_ITEM(LC_TIME, 62)
#define ABALTMON_3 _NL_ITEM(LC_TIME, 63)
#define ABALTMON_4 _NL_ITEM(LC_TIME, 64)
#define ABALTMON_5 _NL_ITEM(LC_TIME, 65)
#define ABALTMON_6 _NL_ITEM(LC_TIME, 66)
#define ABALTMON_7 _NL_ITEM(LC_TIME, 67)
#define ABALTMON_8 _NL_ITEM(LC_TIME, 68)
#define ABALTMON_9 _NL_ITEM(LC_TIME, 69)
#define ABALTMON_10 _NL_ITEM(LC_TIME, 70)
#define ABALTMON_11 _NL_ITEM(LC_TIME, 71)
#define ABALTMON_12 _NL_ITEM(LC_TIME, 72)
#endif

#define CRNCYSTR _NL_ITEM(LC_MONETARY, 0)

// Explicit langinfo.h inclusion also works without the locale.h POSIX names.
#define YESEXPR _NL_ITEM(__LLVM_LIBC_LC_MESSAGES, 0)
#define NOEXPR _NL_ITEM(__LLVM_LIBC_LC_MESSAGES, 1)

#endif // LLVM_LIBC_MACROS_LANGINFO_MACROS_H
