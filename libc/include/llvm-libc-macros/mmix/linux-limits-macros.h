//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_MMIX_LINUX_LIMITS_MACROS_H
#define LLVM_LIBC_MACROS_MMIX_LINUX_LIMITS_MACROS_H

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX2024)
// Highest supported signal plus one, not the public signal-set capacity.
#define NSIG_MAX 65
#endif

#endif // LLVM_LIBC_MACROS_MMIX_LINUX_LIMITS_MACROS_H
