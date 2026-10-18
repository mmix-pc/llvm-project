//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_PROCESS_EXIT_H
#define LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_PROCESS_EXIT_H
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
// PLATFORM elects one normal/quick exit owner; recursive exit is immediate.
void begin_process_exit(int status);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
