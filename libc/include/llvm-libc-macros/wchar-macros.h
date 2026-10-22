//===-- Macros defined in wchar.h header file -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_WCHAR_MACROS_H
#define LLVM_LIBC_MACROS_WCHAR_MACROS_H

#include "../llvm-libc-types/wint_t.h"

#ifndef WEOF
#define WEOF ((wint_t)(0xffffffffu))
#endif

#ifndef WCHAR_MAX
#define WCHAR_MAX __WCHAR_MAX__
#endif

#ifndef WCHAR_MIN
#ifdef __WCHAR_MIN__
#define WCHAR_MIN __WCHAR_MIN__
#elif defined(__WCHAR_UNSIGNED__)
#define WCHAR_MIN (__WCHAR_MAX__ - __WCHAR_MAX__)
#else
#define WCHAR_MIN (-__WCHAR_MAX__ - 1)
#endif
#endif

#endif // LLVM_LIBC_MACROS_WCHAR_MACROS_H
