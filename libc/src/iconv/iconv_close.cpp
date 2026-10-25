//===-- Implementation of iconv_close -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/iconv/iconv_close.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/iconv/context.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, iconv_close, (iconv_t descriptor)) {
  if (!descriptor || descriptor == reinterpret_cast<iconv_t>(-1)) {
    libc_errno = EBADF;
    return -1;
  }
  reinterpret_cast<internal::IconvContext *>(descriptor)->close();
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
