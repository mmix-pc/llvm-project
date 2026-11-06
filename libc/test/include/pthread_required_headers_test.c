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
int (*yield_function)(void) = sched_yield;
int (*clock_function)(clockid_t, struct timespec *) = clock_gettime;
time_t (*time_function)(time_t *) = time;
int main(void) { return 0; }
