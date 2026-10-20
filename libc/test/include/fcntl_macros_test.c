//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <fcntl.h>

_Static_assert((O_LARGEFILE & O_DIRECTORY) == 0,
               "large files must not imply directories");
_Static_assert(F_GETLK == 5 && F_SETLK == 6 && F_SETLKW == 7,
               "feature macros must not rewrite ordinary commands");
#if __SIZEOF_POINTER__ == 8 || defined(__x86_64__)
_Static_assert(F_GETLK64 == 5 && F_SETLK64 == 6 && F_SETLKW64 == 7,
               "native 64-bit lock commands");
#else
_Static_assert(F_GETLK64 == 12 && F_SETLK64 == 13 && F_SETLKW64 == 14,
               "32-bit kernel large-file lock commands");
#endif

int main(void) { return 0; }
