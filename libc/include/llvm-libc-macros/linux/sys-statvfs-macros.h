//===-- Linux macros defined in sys/statvfs.h ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_LINUX_SYS_STATVFS_MACROS_H
#define LLVM_LIBC_MACROS_LINUX_SYS_STATVFS_MACROS_H

#define ST_RDONLY 0x0001
#define ST_NOSUID 0x0002

#ifdef _GNU_SOURCE
#define ST_NODEV 0x0004
#define ST_NOEXEC 0x0008
#define ST_SYNCHRONOUS 0x0010
#define ST_MANDLOCK 0x0040
// Historical GNU flags; Linux reserves these bits.
#define ST_WRITE 0x0080
#define ST_APPEND 0x0100
#define ST_IMMUTABLE 0x0200
#define ST_NOATIME 0x0400
#define ST_NODIRATIME 0x0800
#define ST_RELATIME 0x1000
#define ST_NOSYMFOLLOW 0x2000
#endif

#endif // LLVM_LIBC_MACROS_LINUX_SYS_STATVFS_MACROS_H
