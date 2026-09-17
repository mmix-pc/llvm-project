//===-- MMIX Linux command-environment paths ----------------------*- C -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_MMIX_PATHS_H
#define LLVM_LIBC_MMIX_PATHS_H

#define _PATH_BSHELL "/bin/sh"
#define _PATH_CONSOLE "/dev/console"
#define _PATH_DEVNULL "/dev/null"
#define _PATH_TTY "/dev/tty"
#define _PATH_DEFPATH "/usr/bin:/bin"
#define _PATH_STDPATH "/usr/sbin:/usr/bin:/sbin:/bin"
#define _PATH_PASSWD "/etc/passwd"
#define _PATH_GROUP "/etc/group"
#define _PATH_MOUNTED "/etc/mtab"
#define _PATH_MNTTAB "/etc/fstab"

#endif
