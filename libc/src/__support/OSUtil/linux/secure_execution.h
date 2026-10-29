//===-- Linux runtime security query ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SECURE_EXECUTION_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SECURE_EXECUTION_H
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
bool is_secure_execution();
} // namespace LIBC_NAMESPACE_DECL
#endif
