//===-- Generic error number macro tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Include the generic provider directly, even when testing on a Linux host.
#include "include/llvm-libc-macros/generic-error-number-macros.h"

_Static_assert(EOVERFLOW == 75, "generic overflow error number");
_Static_assert(EOPNOTSUPP == 95, "generic unsupported operation error number");
_Static_assert(ENOTSUP == EOPNOTSUPP, "unsupported operation alias");
_Static_assert(EDOM == 33 && ERANGE == 34 && EILSEQ == 84,
               "existing generic error numbers remain unchanged");

int main(void) { return 0; }
