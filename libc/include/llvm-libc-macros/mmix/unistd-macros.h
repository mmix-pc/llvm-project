//===-- MMIX Linux unistd capability macros -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H
#define LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H

// Installed capability values describe the selected provider, not client flags.
#if __has_include("llvm-libc-macros/mmix/unistd-options.h")
#include "llvm-libc-macros/mmix/unistd-options.h"
#else
// Source-only and preparation headers do not promise a public pthread runtime.
#define _POSIX_THREADS (-1L)
#define _POSIX_THREAD_ATTR_STACKADDR (-1L)
#define _POSIX_THREAD_ATTR_STACKSIZE (-1L)
#define _POSIX_THREAD_PRIORITY_SCHEDULING (-1L)
#define _POSIX_THREAD_PRIO_INHERIT (-1L)
#define _POSIX_THREAD_PRIO_PROTECT (-1L)
#define _POSIX_THREAD_PROCESS_SHARED (-1L)
#define _POSIX_THREAD_CPUTIME (-1L)
#define _POSIX_THREAD_ROBUST_PRIO_INHERIT (-1L)
#define _POSIX_THREAD_ROBUST_PRIO_PROTECT (-1L)
#define _POSIX_BARRIERS (-1L)
#define _POSIX_READER_WRITER_LOCKS (-1L)
#define _POSIX_SPIN_LOCKS (-1L)
#endif

#define _SC_THREAD_ATTR_STACKADDR 77
#define _SC_THREAD_ATTR_STACKSIZE 78
#define _SC_THREAD_PRIORITY_SCHEDULING 79
#define _SC_THREAD_PRIO_INHERIT 80
#define _SC_THREAD_PRIO_PROTECT 81
#define _SC_THREAD_PROCESS_SHARED 82
#define _SC_THREAD_CPUTIME 139
#define _SC_THREAD_ROBUST_PRIO_INHERIT 247
#define _SC_THREAD_ROBUST_PRIO_PROTECT 248
#define _SC_BARRIERS 133
#define _SC_READER_WRITER_LOCKS 153
#define _SC_SPIN_LOCKS 154

#endif // LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H
