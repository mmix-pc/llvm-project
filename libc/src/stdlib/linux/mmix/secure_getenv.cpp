//===-- MMIX Linux secure_getenv implementation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/secure_getenv.h"
#include "src/__support/OSUtil/linux/mmix/startup_security.h"
#include "src/__support/common.h"
#include "src/stdlib/getenv.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, secure_getenv, (const char *name)) {
  return mmix::secure_execution ? nullptr : LIBC_NAMESPACE::getenv(name);
}
} // namespace LIBC_NAMESPACE_DECL
