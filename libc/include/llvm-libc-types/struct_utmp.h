//===-- Native Linux user accounting record ----------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_UTMP_H
#define LLVM_LIBC_TYPES_STRUCT_UTMP_H

#include "pid_t.h"
#include "struct_timeval.h"

struct exit_status {
  short e_termination;
  short e_exit;
};

// Native-endian GNU Linux layout, without the x86 compat32 time/session ABI.
struct utmp {
  short ut_type;
  pid_t ut_pid;
  char ut_line[32];
  char ut_id[4];
  char ut_user[32];
  char ut_host[256];
  struct exit_status ut_exit;
  long ut_session;
  struct timeval ut_tv;
  __INT32_TYPE__ ut_addr_v6[4];
  char __glibc_reserved[20];
};

#endif // LLVM_LIBC_TYPES_STRUCT_UTMP_H
