//===-- Traditional user accounting constants ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_UTMP_MACROS_H
#define LLVM_LIBC_MACROS_UTMP_MACROS_H

#include "utmpx-macros.h"

#define UTMP_FILE _PATH_UTMP
#define UTMP_FILENAME _PATH_UTMP
#define WTMP_FILE _PATH_WTMP
#define WTMP_FILENAME _PATH_WTMP
#define ut_name ut_user
#define ut_time ut_tv.tv_sec
#define ut_xtime ut_tv.tv_sec
#define ut_addr ut_addr_v6[0]

#endif // LLVM_LIBC_MACROS_UTMP_MACROS_H
