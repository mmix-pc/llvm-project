//===-- Pipe stream entrypoint helpers --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_STDIO_LINUX_PIPE_FILE_H
#define LLVM_LIBC_SRC_STDIO_LINUX_PIPE_FILE_H
#include "src/__support/File/file.h"
namespace LIBC_NAMESPACE_DECL {
File *open_pipe(const char *command, const char *mode);
int close_pipe(File *file);
} // namespace LIBC_NAMESPACE_DECL
#endif
