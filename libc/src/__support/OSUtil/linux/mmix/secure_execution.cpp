//===-- MMIX runtime security query ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/__support/OSUtil/linux/secure_execution.h"
#include "src/__support/OSUtil/linux/mmix/startup_security.h"
namespace LIBC_NAMESPACE_DECL {
bool is_secure_execution() { return mmix::secure_execution; }
} // namespace LIBC_NAMESPACE_DECL
