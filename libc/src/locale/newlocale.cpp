//===-- Implementation of newlocale ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/newlocale.h"
#include "hdr/errno_macros.h"
#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/locale/locale.h"
#include "src/locale/locale_storage.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(locale_t, newlocale,
                   (int category_mask, const char *locale_name,
                    locale_t base)) {
  if (category_mask == LC_ALL_MASK)
    category_mask = internal::LOCALE_MASK;
  if ((category_mask & ~internal::LOCALE_MASK) != 0 || !locale_name ||
      base == LC_GLOBAL_LOCALE) {
    libc_errno = EINVAL;
    return nullptr;
  }
  auto result = internal::resolve_locale(
      category_mask, locale_name, base ? *base : internal::locale_from_mask(0));
  if (!result) {
    libc_errno = result.error();
    return nullptr;
  }
  // Reuse base only after successful resolution; failure leaves it untouched.
  if (!base) {
    base = internal::allocate_locale_object(*result);
  } else {
    internal::release_locale(*base);
  }
  if (!base) {
    internal::release_locale(*result);
    libc_errno = ENOMEM;
    return nullptr;
  }
  internal::update_locale_object(base, *result);
  return base;
}

} // namespace LIBC_NAMESPACE_DECL
