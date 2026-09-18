//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/putc_unlocked.h"
#include "src/__support/common.h"
#include "src/stdio/fputc_unlocked.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, putc_unlocked, (int c, ::FILE *stream)) {
  return LIBC_NAMESPACE::fputc_unlocked(c, stream);
}
} // namespace LIBC_NAMESPACE_DECL
