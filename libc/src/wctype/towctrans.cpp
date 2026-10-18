//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wctype/towctrans.h"
#include "src/__support/common.h"
#include "src/wctype/towlower.h"
#include "src/wctype/towupper.h"
#include "src/wctype/wctrans.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(wint_t, towctrans, (wint_t wc, wctrans_t desc)) {
  if (desc == LIBC_NAMESPACE::wctrans("tolower"))
    return LIBC_NAMESPACE::towlower(wc);
  if (desc == LIBC_NAMESPACE::wctrans("toupper"))
    return LIBC_NAMESPACE::towupper(wc);
  return wc;
}
} // namespace LIBC_NAMESPACE_DECL
