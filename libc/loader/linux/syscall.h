//===-- Loader raw syscall boundary ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_SYSCALL_H
#define LLVM_LIBC_LOADER_LINUX_SYSCALL_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Return the kernel result, including negative errno values, without TLS.
long raw_syscall(long number, long a1 = 0, long a2 = 0, long a3 = 0,
                 long a4 = 0, long a5 = 0, long a6 = 0);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_SYSCALL_H
