//===-- Implementation of towctrans_l -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wctype/towctrans_l.h"
#include "src/__support/common.h"
#include "src/locale/character.h"
#include "src/wctype/wctrans.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(wint_t, towctrans_l,
                   (wint_t c, wctrans_t desc, locale_t locale)) {
  if (desc == LIBC_NAMESPACE::wctrans("tolower"))
    return internal::map_wide(c, false, internal::explicit_ctype(locale));
  if (desc == LIBC_NAMESPACE::wctrans("toupper"))
    return internal::map_wide(c, true, internal::explicit_ctype(locale));
  return c;
}

} // namespace LIBC_NAMESPACE_DECL
