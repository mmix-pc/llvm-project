//===-- MMIX Linux time macros ------------------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __LLVM_LIBC_MACROS_MMIX_LINUX_TIME_MACROS_H
#define __LLVM_LIBC_MACROS_MMIX_LINUX_TIME_MACROS_H

#include "../linux/time-macros.h"

// CLOCKS_PER_SEC must have type clock_t, which is long in this ABI.
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000000L

#endif
