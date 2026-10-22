//===-- Per-object allocation queries --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UNISTD_LINUX_FALLOCATE_QUERY_H
#define LLVM_LIBC_SRC_UNISTD_LINUX_FALLOCATE_QUERY_H

#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

ErrorOr<long> fallocate_support(int fd);
ErrorOr<long> fallocate_support(const char *path);

} // namespace LIBC_NAMESPACE_DECL

#endif
