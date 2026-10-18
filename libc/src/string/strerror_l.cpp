//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/strerror_l.h"
#include "src/__support/StringUtil/error_to_string.h"
#include "src/__support/common.h"
#include "src/__support/libc_assert.h"
#include "src/locale/locale.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, strerror_l, (int errnum, locale_t loc)) {
  // Only C locale objects can currently be constructed. Add message selection
  // here when the locale provider gains other locales.
  LIBC_REQUIRE(loc == &c_locale, "strerror_l requires a supported locale");
  // Unknown messages share the calling thread's error-to-string buffer.
  return const_cast<char *>(get_error_string(errnum).data());
}

} // namespace LIBC_NAMESPACE_DECL
