// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_UNISTD_LINUX_USERSHELL_H
#define LLVM_LIBC_SRC_UNISTD_LINUX_USERSHELL_H
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace internal {
char *next_shell();
void reset_shells();
void end_shells();
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
