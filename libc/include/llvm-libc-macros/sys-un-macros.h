//===-- Macros for sys/un.h -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_SYS_UN_MACROS_H
#define LLVM_LIBC_MACROS_SYS_UN_MACROS_H

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_DEFAULT)
#define SUN_LEN(ptr)                                                           \
  (__builtin_offsetof(struct sockaddr_un, sun_path) +                          \
   __builtin_strlen((ptr)->sun_path))
#endif

#endif // LLVM_LIBC_MACROS_SYS_UN_MACROS_H
