//===-- Implementation of wctob -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wctob.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/locale/encoding.h"

#include "hdr/stdio_macros.h" // for EOF.
#include "hdr/types/wint_t.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, wctob, (wint_t c)) {
  // The standard states that wint_t may either be an alias of wchar_t or
  // an alias of an integer type, different platforms define this type with
  // different signedness. Masking also
  // works without -Wtype-limits warnings when `wint_t` is unsigned.
  const wint_t mask = internal::locale_is_utf8() ? 127 : 255;
  if ((c & ~mask) != 0)
    return EOF;
  return static_cast<int>(c);
}

} // namespace LIBC_NAMESPACE_DECL
