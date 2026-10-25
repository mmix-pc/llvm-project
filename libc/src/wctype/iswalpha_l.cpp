//===-- Implementation of iswalpha_l
//----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wctype/iswalpha_l.h"
#include "src/__support/common.h"
#include "src/locale/character.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, iswalpha_l, (wint_t c, locale_t locale)) {
  return internal::classify_wide(c, internal::WCTYPE_ALPHA,
                                 internal::explicit_ctype(locale));
}

} // namespace LIBC_NAMESPACE_DECL
