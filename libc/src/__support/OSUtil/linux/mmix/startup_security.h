//===-- MMIX startup security snapshot --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_STARTUP_SECURITY_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_STARTUP_SECURITY_H
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Written exactly once by static CRT or shared adoption before constructors.
// Kept independently of environ and the application-visible auxv storage.
[[gnu::visibility("hidden")]] extern bool secure_execution;
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
