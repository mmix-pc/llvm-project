//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/vwprintf.h"
#include "src/__support/common.h"
#include "src/stdio/stdout.h"
#include "src/wchar/vfwprintf.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, vwprintf,
                   (const wchar_t *__restrict format, va_list args)) {
  return LIBC_NAMESPACE::vfwprintf(LIBC_NAMESPACE::stdout, format, args);
}
} // namespace LIBC_NAMESPACE_DECL
