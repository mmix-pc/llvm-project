//===-- MMIX Linux programming environment publication --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define _XOPEN_SOURCE 800
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

#if _POSIX_V7_LP64_OFF64 != 1 || _POSIX_V8_LP64_OFF64 != 1
#error "MMIX Linux must publish its native LP64 compilation environment"
#endif
#if _POSIX_V7_ILP32_OFF32 > 0 || _POSIX_V7_ILP32_OFFBIG > 0 ||                      \
    _POSIX_V7_LPBIG_OFFBIG > 0 || _POSIX_V8_ILP32_OFF32 > 0 ||                     \
    _POSIX_V8_ILP32_OFFBIG > 0 || _POSIX_V8_LPBIG_OFFBIG > 0
#error "MMIX Linux does not supply an alternate compilation model"
#endif
_Static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(long) == 8 &&
                   sizeof(off_t) == 8 && sizeof(void *) == 8 &&
                   sizeof(void (*)(void)) == 8,
               "advertised LP64_OFF64 widths");
_Static_assert((off_t)-1 < 0, "signed file offsets");
int main(void) { return 0; }
