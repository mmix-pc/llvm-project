//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/stdio/dprintf.h"
#include "src/__support/common.h"
#include "src/stdio/vdprintf.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, dprintf, (int fd, const char *__restrict format, ...)) {
  va_list args;
  va_start(args, format);
  int result = LIBC_NAMESPACE::vdprintf(fd, format, args);
  va_end(args);
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
