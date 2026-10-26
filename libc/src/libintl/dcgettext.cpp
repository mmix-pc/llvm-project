//===-- Implementation of dcgettext
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/dcgettext.h"
#include "hdr/locale_macros.h"
#include "src/__support/common.h"
#include "src/libintl/message_lookup.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, dcgettext,
                   (const char *domain, const char *singular, int category)) {
  return internal::lookup_message(domain, singular, nullptr, cpp::nullopt,
                                  category, nullptr);
}
} // namespace LIBC_NAMESPACE_DECL
