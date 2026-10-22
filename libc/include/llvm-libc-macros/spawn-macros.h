//===-- Macros for spawn.h -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_SPAWN_MACROS_H
#define LLVM_LIBC_MACROS_SPAWN_MACROS_H

#define POSIX_SPAWN_RESETIDS 0x01
#define POSIX_SPAWN_SETPGROUP 0x02
#define POSIX_SPAWN_SETSIGDEF 0x04
#define POSIX_SPAWN_SETSIGMASK 0x08
#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER 0x20
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || __LIBC_USE_POSIX2024
#define POSIX_SPAWN_SETSID 0x80
#endif

struct sched_param;

#endif // LLVM_LIBC_MACROS_SPAWN_MACROS_H
