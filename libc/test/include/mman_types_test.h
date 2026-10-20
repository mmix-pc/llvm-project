//===-- Tests for sys/mman.h function types -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sys/mman.h>

// Unevaluated calls also check that read-only address expressions are accepted.
#ifdef __cplusplus
static_assert(sizeof(mlock(static_cast<const void *>(nullptr), 0)) == sizeof(int));
static_assert(sizeof(mlock2(static_cast<const void *>(nullptr), 0, 0)) ==
              sizeof(int));
static_assert(sizeof(munlock(static_cast<const void *>(nullptr), 0)) ==
              sizeof(int));
#else
_Static_assert(_Generic(&mlock, int (*)(const void *, size_t): 1, default: 0),
               "mlock takes a const address");
_Static_assert(
    _Generic(&mlock2, int (*)(const void *, size_t, unsigned int): 1, default: 0),
    "mlock2 takes a const address");
_Static_assert(_Generic(&munlock, int (*)(const void *, size_t): 1, default: 0),
               "munlock takes a const address");
#endif
