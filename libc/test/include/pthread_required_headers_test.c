//===-- Required pthread scheduling and time visibility -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define _XOPEN_SOURCE 800
#include <pthread.h>

_Static_assert(SCHED_FIFO != SCHED_RR && SCHED_RR != SCHED_OTHER &&
                   SCHED_FIFO != SCHED_OTHER,
               "distinct scheduling policies from pthread.h");
_Static_assert(CLOCKS_PER_SEC > 0 && TIME_UTC > 0, "time constants");
struct tm calendar;
struct itimerspec timer;
struct sched_param priority;
struct timespec timestamp;
_Static_assert(_Generic(&sched_yield, int (*)(void): 1, default: 0),
               "scheduling prototype");
_Static_assert(_Generic(&clock_gettime,
                       int (*)(clockid_t, struct timespec *): 1, default: 0),
               "clock prototype");
_Static_assert(_Generic(&time, time_t (*)(time_t *): 1, default: 0),
               "time prototype");
int main(void) { return 0; }
