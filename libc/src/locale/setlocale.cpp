//===-- Implementation of setlocale ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/setlocale.h"
#include "hdr/errno_macros.h"
#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/locale/locale.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, setlocale, (int category, const char *locale_name)) {
  if (category < 0 || category > LC_ALL) {
    libc_errno = EINVAL;
    return nullptr;
  }
  auto result = internal::global_locale_snapshot();
  if (locale_name) {
    int mask = category == LC_ALL ? internal::LOCALE_MASK : 1 << category;
    if (!internal::resolve_locale(mask, locale_name, result)) {
      libc_errno = ENOENT;
      return nullptr;
    }
    internal::publish_global_locale(result);
  }
  return internal::locale_name(result, category);
}

} // namespace LIBC_NAMESPACE_DECL
