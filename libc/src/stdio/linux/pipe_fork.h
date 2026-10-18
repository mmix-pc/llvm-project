//===-- Fork callbacks for pipe stream ownership ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_STDIO_LINUX_PIPE_FORK_H
#define LLVM_LIBC_SRC_STDIO_LINUX_PIPE_FORK_H
#include "src/__support/threads/fork_callbacks.h"
namespace LIBC_NAMESPACE_DECL {
bool register_pipe_fork_callbacks(ForkCallback *prepare, ForkCallback *parent,
                                  ForkCallback *child);
} // namespace LIBC_NAMESPACE_DECL
#endif
