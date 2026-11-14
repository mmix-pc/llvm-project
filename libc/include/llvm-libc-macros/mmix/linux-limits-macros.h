//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __LLVM_LIBC_MACROS_MMIX_LINUX_LIMITS_MACROS_H
#define __LLVM_LIBC_MACROS_MMIX_LINUX_LIMITS_MACROS_H

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX)
// Maximum terminal-name storage, including the terminating NUL.
#define TTY_NAME_MAX 4096
// Fixed Linux UAPI message-priority count, independent of queue quotas.
#define MQ_PRIO_MAX 32768
#endif

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX2024)
// Highest supported signal plus one, not the public signal-set capacity.
#define NSIG_MAX 65
#endif

#endif // __LLVM_LIBC_MACROS_MMIX_LINUX_LIMITS_MACROS_H
