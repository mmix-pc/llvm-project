//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-types/iconv_t.h"
#include "include/llvm-libc-types/wordexp_t.h"
#include "include/llvm-libc-macros/wordexp-macros.h"

#ifdef __cplusplus
#define CHECK static_assert
#define SAME_TYPE(a, b) __is_same(decltype(a), decltype(b))
#else
#define CHECK _Static_assert
#define SAME_TYPE(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif

CHECK(sizeof(iconv_t) == sizeof(void *), "opaque descriptor pointer");
CHECK(SAME_TYPE(((wordexp_t *)0)->we_wordc, (size_t)0), "count type");
CHECK(SAME_TYPE(((wordexp_t *)0)->we_wordv, (char **)0), "vector type");
CHECK(SAME_TYPE(((wordexp_t *)0)->we_offs, (size_t)0), "offset type");
CHECK(__builtin_offsetof(wordexp_t, we_wordc) == 0, "first member");
CHECK(__builtin_offsetof(wordexp_t, we_wordv) >
          __builtin_offsetof(wordexp_t, we_wordc), "vector follows count");
CHECK(__builtin_offsetof(wordexp_t, we_offs) >
          __builtin_offsetof(wordexp_t, we_wordv), "offset follows vector");
CHECK((WRDE_DOOFFS | WRDE_APPEND | WRDE_NOCMD | WRDE_REUSE |
       WRDE_SHOWERR | WRDE_UNDEF) == 63, "independent flag bits");
CHECK(WRDE_NOSPACE == 1 && WRDE_BADCHAR == 2 && WRDE_BADVAL == 3 &&
          WRDE_CMDSUB == 4 && WRDE_SYNTAX == 5, "error identifiers");

int main(void) { return 0; }
