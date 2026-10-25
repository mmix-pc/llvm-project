//===-- Implementation of wcsncasecmp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcsncasecmp.h"
#include "src/__support/common.h"
#include "src/wchar/case_compare.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, wcsncasecmp,
                   (const wchar_t *left, const wchar_t *right, size_t count)) {
  auto *category = internal::current_ctype();
  return internal::compare_wide_case(left, right, count, category);
}
} // namespace LIBC_NAMESPACE_DECL
