//===-- MMIX Linux concurrency hints ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CONCURRENCY_HINT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CONCURRENCY_HINT_H
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int get_concurrency_hint();
int set_concurrency_hint(int value);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
