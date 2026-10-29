//===-- Linux runtime security query --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/__support/OSUtil/linux/secure_execution.h"
#include "src/__support/OSUtil/linux/auxv.h"
namespace LIBC_NAMESPACE_DECL {
bool is_secure_execution() {
  // Preserve the existing auxv-based policy on startup providers that do not
  // yet publish a separate security snapshot.
  auto secure = auxv::get(AT_SECURE);
  return !secure || *secure != 0;
}
} // namespace LIBC_NAMESPACE_DECL
