//===-- Implementation of dgettext_l
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/dgettext_l.h"
#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/libintl/message_lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, dgettext_l,
                   (const char *domain, const char *singular,
                    locale_t locale)) {
  return internal::lookup_message(domain, singular, nullptr, cpp::nullopt,
                                  LC_MESSAGES, locale);
}
} // namespace LIBC_NAMESPACE_DECL
