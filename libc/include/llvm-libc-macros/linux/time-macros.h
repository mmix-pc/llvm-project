//===-- Definition of macros from time.h ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_LINUX_TIME_MACROS_H
#define LLVM_LIBC_MACROS_LINUX_TIME_MACROS_H

// clock type macros
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX1993)
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#endif
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_DEFAULT)
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#define CLOCK_REALTIME_ALARM 8
#define CLOCK_BOOTTIME_ALARM 9
#endif

#define CLOCKS_PER_SEC 1000000

// timer flags
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX1993)
#define TIMER_ABSTIME 1
#endif

#endif // LLVM_LIBC_MACROS_LINUX_TIME_MACROS_H
