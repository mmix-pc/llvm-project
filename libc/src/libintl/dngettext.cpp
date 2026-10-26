//===-- Implementation of dngettext
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/dngettext.h"
#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/libintl/message_lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, dngettext,
                   (const char *domain, const char *singular,
                    const char *plural, unsigned long count)) {
  return internal::lookup_message(domain, singular, plural, count, LC_MESSAGES,
                                  nullptr);
}
} // namespace LIBC_NAMESPACE_DECL
