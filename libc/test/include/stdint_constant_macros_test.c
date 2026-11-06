//===-- Integer constant macro promotion regression ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/stdint-macros.h"

#define SAME_TYPE(a, b) _Generic((a), __typeof__(b): 1, default: 0)

_Static_assert(SAME_TYPE(INT8_C(1), +(int_least8_t)0), "INT8_C promoted type");
_Static_assert(SAME_TYPE(INT8_C(0x7f), +(int_least8_t)0), "INT8_C hex type");
_Static_assert(INT8_C(127) == 127LL, "INT8_C value");
#if INT8_C(127) != 127LL || INT8_C(0) != 0
#error INT8_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(UINT8_C(1), +(uint_least8_t)0), "UINT8_C promoted type");
_Static_assert(SAME_TYPE(UINT8_C(0xff), +(uint_least8_t)0), "UINT8_C hex type");
_Static_assert(UINT8_C(255) == 255ULL, "UINT8_C value");
#if UINT8_C(255) != 255ULL || UINT8_C(0) != 0
#error UINT8_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(INT16_C(1), +(int_least16_t)0), "INT16_C promoted type");
_Static_assert(SAME_TYPE(INT16_C(0x7fff), +(int_least16_t)0), "INT16_C hex type");
_Static_assert(INT16_C(32767) == 32767LL, "INT16_C value");
#if INT16_C(32767) != 32767LL || INT16_C(0) != 0
#error INT16_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(UINT16_C(1), +(uint_least16_t)0), "UINT16_C promoted type");
_Static_assert(SAME_TYPE(UINT16_C(0xffff), +(uint_least16_t)0), "UINT16_C hex type");
_Static_assert(UINT16_C(65535) == 65535ULL, "UINT16_C value");
#if UINT16_C(65535) != 65535ULL || UINT16_C(0) != 0
#error UINT16_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(INT32_C(1), +(int_least32_t)0), "INT32_C promoted type");
_Static_assert(SAME_TYPE(INT32_C(0x7fffffff), +(int_least32_t)0), "INT32_C hex type");
_Static_assert(INT32_C(2147483647) == 2147483647LL, "INT32_C value");
#if INT32_C(2147483647) != 2147483647LL || INT32_C(0) != 0
#error INT32_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(UINT32_C(1), +(uint_least32_t)0), "UINT32_C promoted type");
_Static_assert(SAME_TYPE(UINT32_C(0xffffffff), +(uint_least32_t)0), "UINT32_C hex type");
_Static_assert(UINT32_C(4294967295) == 4294967295ULL, "UINT32_C value");
#if UINT32_C(4294967295) != 4294967295ULL || UINT32_C(0) != 0
#error UINT32_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(INT64_C(1), +(int_least64_t)0), "INT64_C promoted type");
_Static_assert(SAME_TYPE(INT64_C(0x7fffffffffffffff), +(int_least64_t)0), "INT64_C hex type");
_Static_assert(INT64_C(9223372036854775807) == 9223372036854775807LL, "INT64_C value");
#if INT64_C(9223372036854775807) != 9223372036854775807LL || INT64_C(0) != 0
#error INT64_C must be usable in preprocessing expressions
#endif

_Static_assert(SAME_TYPE(UINT64_C(1), +(uint_least64_t)0), "UINT64_C promoted type");
_Static_assert(SAME_TYPE(UINT64_C(0xffffffffffffffff), +(uint_least64_t)0), "UINT64_C hex type");
_Static_assert(UINT64_C(18446744073709551615) == 18446744073709551615ULL, "UINT64_C value");
#if UINT64_C(18446744073709551615) != 18446744073709551615ULL || UINT64_C(0) != 0
#error UINT64_C must be usable in preprocessing expressions
#endif

int main(void) { return 0; }
