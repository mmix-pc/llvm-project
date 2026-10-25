//===-- Wide case-insensitive comparison -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WCHAR_CASE_COMPARE_H
#define LLVM_LIBC_SRC_WCHAR_CASE_COMPARE_H

#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/locale/character.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

inline int compare_wide_case(const wchar_t *left, const wchar_t *right,
                             size_t count, const __locale_data *category) {
  while (count--) {
    wchar_t a = static_cast<wchar_t>(map_wide(*left++, false, category));
    wchar_t b = static_cast<wchar_t>(map_wide(*right++, false, category));
    if (a != b)
      return a < b ? -1 : 1;
    if (!a)
      break;
  }
  return 0;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_WCHAR_CASE_COMPARE_H
