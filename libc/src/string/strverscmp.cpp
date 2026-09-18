//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/strverscmp.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, strverscmp, (const char *left, const char *right)) {
  auto digit = [](unsigned char c) { return c >= '0' && c <= '9'; };
  const auto *l = reinterpret_cast<const unsigned char *>(left);
  const auto *r = reinterpret_cast<const unsigned char *>(right);
  const unsigned char *run = l;
  bool zeros = true;
  while (*l == *r) {
    if (*l == 0)
      return 0;
    if (!digit(*l)) {
      run = l + 1;
      zeros = true;
    } else if (*l != '0') {
      zeros = false;
    }
    ++l;
    ++r;
  }

  if (*run != '0' && *(r - (l - run)) != '0' &&
      (run != l || (digit(*l) && digit(*r)))) {
    // Compare integer runs without converting them to a bounded integer.
    const auto *a = l;
    const auto *b = r;
    while (digit(*a) && digit(*b)) {
      ++a;
      ++b;
    }
    if (digit(*a) != digit(*b))
      return digit(*a) ? 1 : -1;
  } else if (zeros && run != l && (digit(*l) != digit(*r))) {
    // A longer all-zero prefix sorts before its shorter counterpart.
    return digit(*l) ? -1 : 1;
  }
  return int(*l) - int(*r);
}

} // namespace LIBC_NAMESPACE_DECL
