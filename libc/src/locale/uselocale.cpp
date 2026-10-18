//===-- Implementation of uselocale ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/uselocale.h"
#include "src/locale/locale.h"

#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(locale_t, uselocale, (locale_t newloc)) {
  locale_t previous = internal::thread_locale;
  if (newloc)
    internal::thread_locale = newloc;
  return previous;
}

} // namespace LIBC_NAMESPACE_DECL
