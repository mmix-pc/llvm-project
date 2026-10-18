//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

_Static_assert(FILENAME_MAX > 0, "file name buffer size");
_Static_assert(FOPEN_MAX >= 8, "C requires at least eight open streams");
_Static_assert(_Generic(MB_CUR_MAX, size_t: 1, default: 0),
               "MB_CUR_MAX must have type size_t");
_Static_assert(_Generic(WEOF, wint_t: 1, default: 0),
               "wctype.h must define WEOF with type wint_t");

int main(void) { return MB_CUR_MAX < 1; }
