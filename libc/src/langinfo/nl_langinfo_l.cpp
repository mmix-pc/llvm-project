//===-- nl_langinfo_l implementation
//---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/langinfo/nl_langinfo_l.h"
#include "src/__support/common.h"
#include "src/locale/locale.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, nl_langinfo_l, (nl_item item, locale_t loc)) {
  return internal::langinfo(item, loc);
}
} // namespace LIBC_NAMESPACE_DECL
