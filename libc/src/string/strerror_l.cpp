//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/string/strerror_l.h"
#include "src/__support/StringUtil/diagnostic_message.h"
#include "src/__support/StringUtil/error_to_string.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(char *, strerror_l, (int errnum, locale_t locale)) {
  // Unknown messages share the calling thread's error-to-string buffer.
  return const_cast<char *>(
      get_diagnostic_message(get_error_string(errnum).data(), locale));
}

} // namespace LIBC_NAMESPACE_DECL
