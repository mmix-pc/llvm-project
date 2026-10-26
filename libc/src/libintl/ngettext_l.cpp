//===-- Implementation of ngettext_l
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/ngettext_l.h"
#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/libintl/message_lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, ngettext_l,
                   (const char *singular, const char *plural,
                    unsigned long count, locale_t locale)) {
  return internal::lookup_message(nullptr, singular, plural, count, LC_MESSAGES,
                                  locale);
}
} // namespace LIBC_NAMESPACE_DECL
