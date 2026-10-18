//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/vwscanf.h"
#include "src/__support/common.h"
#include "src/stdio/stdin.h"
#include "src/wchar/vfwscanf.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, vwscanf,
                   (const wchar_t *__restrict format, va_list args)) {
  return LIBC_NAMESPACE::vfwscanf(LIBC_NAMESPACE::stdin, format, args);
}
} // namespace LIBC_NAMESPACE_DECL
