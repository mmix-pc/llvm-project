//===-- MMIX Linux mount-table declarations -----------------------*- C -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_MMIX_MNTENT_H
#define LLVM_LIBC_MMIX_MNTENT_H

#include <__llvm-libc-common.h>
#include <stdio.h>

#define MNTTAB "/etc/fstab"
#define MOUNTED "/etc/mtab"

struct mntent {
  char *mnt_fsname;
  char *mnt_dir;
  char *mnt_type;
  char *mnt_opts;
  int mnt_freq;
  int mnt_passno;
};

__BEGIN_C_DECLS
FILE *setmntent(const char *, const char *);
struct mntent *getmntent(FILE *);
struct mntent *getmntent_r(FILE *, struct mntent *, char *, int);
int endmntent(FILE *);
char *hasmntopt(const struct mntent *, const char *) __NOEXCEPT;
__END_C_DECLS

#endif
