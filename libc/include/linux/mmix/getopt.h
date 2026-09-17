//===-- MMIX Linux option parsing declarations --------------------*- C -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_MMIX_GETOPT_H
#define LLVM_LIBC_MMIX_GETOPT_H

#include <__llvm-libc-common.h>
#include <unistd.h>

#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

__BEGIN_C_DECLS
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int, char *const[], const char *) __NOEXCEPT;
int getopt_long(int, char *const[], const char *, const struct option *,
                int *) __NOEXCEPT;
__END_C_DECLS

#endif
