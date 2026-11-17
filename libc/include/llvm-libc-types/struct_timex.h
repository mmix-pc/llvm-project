// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_TYPES_STRUCT_TIMEX_H
#define LLVM_LIBC_TYPES_STRUCT_TIMEX_H
#include "struct_timeval.h"
struct timex {
  unsigned int modes;
  long offset, freq, maxerror, esterror;
  int status;
  long constant, precision, tolerance;
  struct timeval time;
  long tick, ppsfreq, jitter;
  int shift;
  long stabil, jitcnt, calcnt, errcnt, stbcnt;
  int tai;
  int __padding[11];
};
#endif
