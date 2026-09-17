//===-- MMIX Linux process-time declarations ----------------------*- C -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_MMIX_TIMES_H
#define LLVM_LIBC_MMIX_TIMES_H

#include <__llvm-libc-common.h>
#include <llvm-libc-types/clock_t.h>

struct tms {
  clock_t tms_utime;
  clock_t tms_stime;
  clock_t tms_cutime;
  clock_t tms_cstime;
};

__BEGIN_C_DECLS
clock_t times(struct tms *) __NOEXCEPT;
__END_C_DECLS

#endif
