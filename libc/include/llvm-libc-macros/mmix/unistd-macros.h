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
#define _POSIX_FSYNC (-1L)
#define _POSIX_MAPPED_FILES (-1L)
#define _POSIX_MEMORY_PROTECTION (-1L)
#define _POSIX_MEMLOCK (-1L)
#define _POSIX_MEMLOCK_RANGE (-1L)
#define _POSIX_SYNCHRONIZED_IO (-1L)
#define _POSIX_SEMAPHORES (-1L)
#define _POSIX_SHARED_MEMORY_OBJECTS (-1L)
#define _POSIX_MONOTONIC_CLOCK (-1L)
#define _POSIX_CLOCK_SELECTION (-1L)
#define _POSIX_CPUTIME (-1L)
#define _POSIX_REALTIME_SIGNALS (-1L)
#define _POSIX_PRIORITY_SCHEDULING (-1L)
#define _POSIX_SPAWN (-1L)
#define _POSIX_SAVED_IDS (-1L)
#define _POSIX_JOB_CONTROL (-1L)
#define _POSIX_REGEXP (-1L)
#define _POSIX_TIMEOUTS (-1L)
#endif

#endif // LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H
