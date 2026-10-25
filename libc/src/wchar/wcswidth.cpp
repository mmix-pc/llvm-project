//===-- Implementation of wcswidth
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcswidth.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/wchar/display_width.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, wcswidth, (const wchar_t *string, size_t count)) {
  const auto *category = internal::current_ctype();
  int total = 0;
  while (count-- && *string) {
    int width = internal::display_width(*string++, category);
    if (width < 0)
      return -1;
    if (width > cpp::numeric_limits<int>::max() - total) {
      libc_errno = EOVERFLOW;
      return -1;
    }
    total += width;
  }
  return total;
}
} // namespace LIBC_NAMESPACE_DECL
