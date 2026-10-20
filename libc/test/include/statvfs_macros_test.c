//===-- Tests for sys/statvfs.h macros -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sys/statvfs.h>

_Static_assert(ST_RDONLY == 0x0001, "Linux read-only flag");
_Static_assert(ST_NOSUID == 0x0002, "Linux nosuid flag");

#ifdef ST_VALID
#error "The kernel's internal validity flag must not be public"
#endif

#ifdef _GNU_SOURCE
_Static_assert(ST_NODEV == 0x0004 && ST_NOEXEC == 0x0008 &&
                   ST_SYNCHRONOUS == 0x0010 && ST_MANDLOCK == 0x0040 &&
                   ST_WRITE == 0x0080 && ST_APPEND == 0x0100 &&
                   ST_IMMUTABLE == 0x0200 && ST_NOATIME == 0x0400 &&
                   ST_NODIRATIME == 0x0800 && ST_RELATIME == 0x1000 &&
                   ST_NOSYMFOLLOW == 0x2000,
               "Linux/GNU filesystem flags");
#else
#if defined(ST_NODEV) || defined(ST_NOEXEC) || defined(ST_SYNCHRONOUS) ||          \
    defined(ST_MANDLOCK) || defined(ST_WRITE) || defined(ST_APPEND) ||            \
    defined(ST_IMMUTABLE) || defined(ST_NOATIME) || defined(ST_NODIRATIME) ||      \
    defined(ST_RELATIME) || defined(ST_NOSYMFOLLOW)
#error "Extension flags require _GNU_SOURCE"
#endif
#endif

int main(void) { return 0; }
