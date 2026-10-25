//===-- Implementation of iconv_open --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/iconv/iconv_open.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/iconv/context.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(iconv_t, iconv_open, (const char *to, const char *from)) {
  auto result = internal::IconvContext::open(to, from);
  if (!result) {
    libc_errno = result.error();
    return reinterpret_cast<iconv_t>(-1);
  }
  return reinterpret_cast<iconv_t>(*result);
}
} // namespace LIBC_NAMESPACE_DECL
