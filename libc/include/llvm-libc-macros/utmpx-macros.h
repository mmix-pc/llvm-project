//===-- User accounting constants ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_UTMPX_MACROS_H
#define LLVM_LIBC_MACROS_UTMPX_MACROS_H

#ifndef _PATH_UTMP
#define _PATH_UTMP "/var/run/utmp"
#endif
#ifndef _PATH_WTMP
#define _PATH_WTMP "/var/log/wtmp"
#endif
#define _PATH_UTMPX _PATH_UTMP
#define _PATH_WTMPX _PATH_WTMP

#define UT_LINESIZE 32
#define UT_NAMESIZE 32
#define UT_HOSTSIZE 256

#define EMPTY 0
#define RUN_LVL 1
#define BOOT_TIME 2
#define NEW_TIME 3
#define OLD_TIME 4
#define INIT_PROCESS 5
#define LOGIN_PROCESS 6
#define USER_PROCESS 7
#define DEAD_PROCESS 8
#define ACCOUNTING 9

#endif // LLVM_LIBC_MACROS_UTMPX_MACROS_H
