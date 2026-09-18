//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/putchar_unlocked.h"
#include "src/__support/common.h"
#include "src/stdio/fputc_unlocked.h"
#include "src/stdio/stdout.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, putchar_unlocked, (int c)) {
  return LIBC_NAMESPACE::fputc_unlocked(c, LIBC_NAMESPACE::stdout);
}
} // namespace LIBC_NAMESPACE_DECL
