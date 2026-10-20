//===-- Proxy header for sys/xattr.h macros ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_HDR_SYS_XATTR_MACROS_H
#define LLVM_LIBC_HDR_SYS_XATTR_MACROS_H

#ifdef LIBC_FULL_BUILD
#include "include/llvm-libc-macros/sys-xattr-macros.h"
#else
#include <sys/xattr.h>
#endif

#endif // LLVM_LIBC_HDR_SYS_XATTR_MACROS_H
