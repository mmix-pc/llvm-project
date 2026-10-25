//===-- Locale name query -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/getlocalename_l.h"
#include "src/__support/common.h"
#include "src/locale/locale_storage.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(const char *, getlocalename_l,
                   (int category, locale_t locale)) {
  if (category < 0 || category > LC_ALL)
    return nullptr;
  if (locale == LC_GLOBAL_LOCALE) {
    // Separate from setlocale's return storage and from all object results.
    static LIBC_THREAD_LOCAL char buffer[internal::LOCALE_NAME_SIZE];
    internal::format_locale_name(internal::global_locale_snapshot(), category,
                                 buffer);
    return buffer;
  }
  return category == LC_ALL ? internal::locale_object_name(locale)
                            : locale->data[category]->name;
}
} // namespace LIBC_NAMESPACE_DECL
