/*===-- MMIX Linux atomic header type dependencies ------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __MMIX_STDATOMIC_TYPES_H
#define __MMIX_STDATOMIC_TYPES_H

/* Match the MMIX Linux explicit POSIX/XSI namespace selection. Extension
 * modes retain their existing complete header imports. */
#if defined(_GNU_SOURCE) || defined(_DEFAULT_SOURCE) ||                      \
    (!defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) &&                  \
     !defined(_XOPEN_SOURCE))
#include <stddef.h>
#include <stdint.h>
#else
/* Only type names are needed by stdatomic.h. The public macro namespaces of
 * stddef.h and stdint.h are not implicitly available to its callers. */
#include <__stddef_ptrdiff_t.h>
#include <__stddef_size_t.h>
#include <__stddef_wchar_t.h>
#if defined(__STDC_WANT_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__ >= 1
#include <__stddef_rsize_t.h>
#endif
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L) ||              \
    defined(__cplusplus)
#include <__stddef_nullptr_t.h>
#endif
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) ||              \
    (defined(__cplusplus) && __cplusplus >= 201103L)
#include <__stddef_max_align_t.h>
#endif

/* Preserve the resource stdint.h type choices, including its least-width
 * choices for the fast types. All these names have the reserved _t suffix. */
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTMAX_TYPE__ intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;
typedef __INT8_TYPE__ int8_t;
typedef __INT_LEAST8_TYPE__ int_least8_t;
typedef __INT_LEAST8_TYPE__ int_fast8_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT_LEAST8_TYPE__ uint_least8_t;
typedef __UINT_LEAST8_TYPE__ uint_fast8_t;
typedef __INT16_TYPE__ int16_t;
typedef __INT_LEAST16_TYPE__ int_least16_t;
typedef __INT_LEAST16_TYPE__ int_fast16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT_LEAST16_TYPE__ uint_least16_t;
typedef __UINT_LEAST16_TYPE__ uint_fast16_t;
typedef __INT32_TYPE__ int32_t;
typedef __INT_LEAST32_TYPE__ int_least32_t;
typedef __INT_LEAST32_TYPE__ int_fast32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT_LEAST32_TYPE__ uint_least32_t;
typedef __UINT_LEAST32_TYPE__ uint_fast32_t;
typedef __INT64_TYPE__ int64_t;
typedef __INT_LEAST64_TYPE__ int_least64_t;
typedef __INT_LEAST64_TYPE__ int_fast64_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __UINT_LEAST64_TYPE__ uint_least64_t;
typedef __UINT_LEAST64_TYPE__ uint_fast64_t;

#endif
#endif /* __MMIX_STDATOMIC_TYPES_H */
