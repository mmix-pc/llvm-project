//===-- Locale selection for diagnostic messages --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/StringUtil/diagnostic_message.h"
#ifdef LIBC_HAS_MESSAGE_CATALOGS
#include "src/libintl/message_lookup.h"
#endif

namespace LIBC_NAMESPACE_DECL {
const char *get_diagnostic_message(const char *message, locale_t locale) {
#ifdef LIBC_HAS_MESSAGE_CATALOGS
  return internal::lookup_diagnostic_message(message, locale);
#else
  (void)locale;
  return message;
#endif
}
} // namespace LIBC_NAMESPACE_DECL
