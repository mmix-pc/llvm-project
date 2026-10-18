//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <limits.h>

_Static_assert(_POSIX_AIO_LISTIO_MAX == 2, "standard minimum");
_Static_assert(_POSIX_CHILD_MAX == 25, "standard minimum");
_Static_assert(_POSIX_CLOCKRES_MIN == 20000000, "standard resolution bound");
_Static_assert(_POSIX_OPEN_MAX == 20, "standard minimum");
_Static_assert(_POSIX_PIPE_BUF == 512, "standard minimum");
_Static_assert(_POSIX_SSIZE_MAX == 32767, "standard minimum");
_Static_assert(_POSIX_THREAD_KEYS_MAX == 128, "standard minimum");
_Static_assert(_POSIX_THREAD_THREADS_MAX == 64, "standard minimum");
_Static_assert(_XOPEN_IOV_MAX == 16, "standard minimum");
_Static_assert(BC_BASE_MAX >= _POSIX2_BC_BASE_MAX, "utility bound");
_Static_assert(BC_DIM_MAX >= _POSIX2_BC_DIM_MAX, "utility bound");
_Static_assert(RE_DUP_MAX == _POSIX2_RE_DUP_MAX, "BSD regex repetition bound");
_Static_assert(RE_DUP_MAX == 255, "standard repetition minimum");
_Static_assert(CHARCLASS_NAME_MAX >= 6, "built-in character class names fit");
_Static_assert(WORD_BIT == sizeof(int) * CHAR_BIT, "int width");
_Static_assert(LONG_BIT == sizeof(long) * CHAR_BIT, "long width");
_Static_assert(NL_ARGMAX >= 9, "positional formatting bound");
_Static_assert(NL_SETMAX > 0 && NL_MSGMAX > 0 && NL_TEXTMAX > 0,
               "catalog representation bounds");
#ifdef __linux__
_Static_assert(NGROUPS_MAX == 65536, "Linux UAPI supplementary group bound");
_Static_assert(NZERO == 20, "Linux priority offset");
#endif

int main(void) { return 0; }
