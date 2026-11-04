//===-- MMIX Linux thread stack defaults ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_STACK_POLICY_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_STACK_POLICY_H

#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
inline constexpr size_t DEFAULT_STACK_SIZE = 2 * 1024 * 1024;
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif
