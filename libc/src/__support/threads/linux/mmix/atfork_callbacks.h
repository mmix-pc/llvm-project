//===-- MMIX Linux fork callback phases -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ATFORK_CALLBACKS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ATFORK_CALLBACKS_H
#include "src/__support/threads/fork_callbacks.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
bool register_fork_callbacks(ForkCallback *prepare, ForkCallback *parent,
                             ForkCallback *child);
size_t prepare_fork_callbacks();
void lock_fork_callbacks();
void finish_fork_callbacks(size_t count, bool child);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
