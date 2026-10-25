//===-- Implementation of wcscasecmp_l -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcscasecmp_l.h"
#include "src/__support/common.h"
#include "src/wchar/case_compare.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, wcscasecmp_l,
                   (const wchar_t *left, const wchar_t *right,
                    locale_t locale)) {
#ifdef LIBC_FULL_BUILD
  auto *category = internal::explicit_ctype(locale);
#else
  (void)locale;
  const __locale_data *category = nullptr;
#endif
  return internal::compare_wide_case(left, right, size_t(-1), category);
}
} // namespace LIBC_NAMESPACE_DECL
