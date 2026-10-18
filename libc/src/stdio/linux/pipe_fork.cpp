//===-- Linux pipe stream fork registration -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/stdio/linux/pipe_fork.h"
namespace LIBC_NAMESPACE_DECL {
bool register_pipe_fork_callbacks(ForkCallback *prepare, ForkCallback *parent,
                                  ForkCallback *child) {
  return register_atfork_callbacks(prepare, parent, child);
}
} // namespace LIBC_NAMESPACE_DECL
